package main

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"
)

/*
Minimal, dependency-free analyzer.

Config format (only what we need):
[analyzer]
includes=[
  "."
]
excludes=[
  "./.venv",
  "./libs/security_warehouse"
]

Notes:
- TOML here is parsed minimally for this specific structure (no third-party lib).
- Includes are starting points to walk; excludes are directory/file prefixes ignored.
- Language detection by extension map below.
- “Source files” = files whose extension is in knownExts; extend as you like.
- Output uses ANSI colors; disable via NO_COLOR=1 if you prefer plain text.
*/

type AnalyzerConfig struct {
	Includes []string
	Excludes []string
}

type FileStat struct {
	Path     string
	Lang     string
	Lines    int
	SizeByte int64
}

type LangAgg struct {
	Files        int
	Lines        int
	Bytes        int64
	PerFileLines []int
}

var (
	// Basic language detection by extension. Add/adjust as needed.
	extToLang = map[string]string{
		".go":    "Go",
		".py":    "Python",
		".js":    "JavaScript",
		".ts":    "TypeScript",
		".jsx":   "JavaScript",
		".tsx":   "TypeScript",
		".java":  "Java",
		".c":     "C",
		".h":     "C/C Header",
		".hpp":   "C++ Header",
		".hh":    "C++ Header",
		".hxx":   "C++ Header",
		".cc":    "C++",
		".cpp":   "C++",
		".cxx":   "C++",
		".rs":    "Rust",
		".rb":    "Ruby",
		".php":   "PHP",
		".cs":    "C#",
		".swift": "Swift",
		".kt":    "Kotlin",
		".m":     "Objective-C",
		".mm":    "Objective-C++",
		".scala": "Scala",
		".sh":    "Shell",
		".bash":  "Shell",
		".zsh":   "Shell",
		".ps1":   "PowerShell",
		".html":  "HTML",
		".css":   "CSS",
		".scss":  "SCSS",
		".sass":  "SASS",
		".sql":   "SQL",
		".yaml":  "YAML",
		".yml":   "YAML",
		".toml":  "TOML",
		".ini":   "INI",
		".cfg":   "Config",
		".proto": "Protobuf",
		".rf":    "Ruleforge",
	}
	knownExts = func() map[string]struct{} {
		s := make(map[string]struct{})
		for k := range extToLang {
			s[k] = struct{}{}
		}
		return s
	}()

	// ANSI styling (respect NO_COLOR)
	useColor = os.Getenv("NO_COLOR") == ""
	cReset   = color("\x1b[0m")
	cBold    = color("\x1b[1m")
	cDim     = color("\x1b[2m")
	cItal    = color("\x1b[3m")
	cHdr     = color("\x1b[38;5;45m")  // cyan-ish
	cOk      = color("\x1b[38;5;82m")  // green
	cWarn    = color("\x1b[38;5;214m") // orange
	cBad     = color("\x1b[38;5;203m") // red
	cNum     = color("\x1b[38;5;39m")  // blue
	cLang    = color("\x1b[38;5;176m") // pink
	cPath    = color("\x1b[38;5;117m") // light cyan
	cSubtle  = color("\x1b[38;5;244m") // grey
)

// color helper that disables if NO_COLOR is set
func color(seq string) string {
	if !useColor {
		return ""
	}
	return seq
}
func style(s string, codes ...string) string {
	if !useColor || len(codes) == 0 {
		return s
	}
	var b strings.Builder
	for _, c := range codes {
		b.WriteString(c)
	}
	b.WriteString(s)
	b.WriteString(cReset)
	return b.String()
}

// ---- Config parsing (very small TOML subset) ----

func parseConfig(path string) (AnalyzerConfig, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return AnalyzerConfig{}, err
	}
	text := string(b)

	sec := extractSection(text, "analyzer")
	if sec == "" {
		return AnalyzerConfig{}, errors.New("missing [analyzer] section")
	}

	includes, err := extractStringArray(sec, "includes")
	if err != nil {
		return AnalyzerConfig{}, fmt.Errorf("includes: %w", err)
	}
	excludes, err := extractStringArray(sec, "excludes")
	if err != nil {
		return AnalyzerConfig{}, fmt.Errorf("excludes: %w", err)
	}

	// Normalize paths
	for i := range includes {
		includes[i] = filepath.Clean(includes[i])
	}
	for i := range excludes {
		excludes[i] = filepath.Clean(excludes[i])
	}

	return AnalyzerConfig{Includes: includes, Excludes: excludes}, nil
}

func extractSection(src, name string) string {
	lines := strings.Split(src, "\n")
	var b strings.Builder
	in := false
	needle := "[" + name + "]"
	for _, ln := range lines {
		trim := strings.TrimSpace(ln)
		if strings.HasPrefix(trim, "[") && strings.HasSuffix(trim, "]") {
			in = trim == needle
			continue
		}
		if in {
			b.WriteString(ln)
			b.WriteByte('\n')
		}
	}
	return b.String()
}

func extractStringArray(section, key string) ([]string, error) {
	// We look for a form like:
	// key = [ ... ]   OR   key=[ ... ]
	// Allow multiline.
	var (
		startIdx = -1
		endIdx   = -1
	)
	// Find "key" occurrence followed by '=' then '['
	idx := strings.Index(section, key)
	for idx != -1 {
		rest := section[idx+len(key):]
		rest = strings.TrimLeft(rest, " \t")
		if len(rest) > 0 && rest[0] == '=' {
			rest2 := strings.TrimLeft(rest[1:], " \t")
			if len(rest2) > 0 && rest2[0] == '[' {
				// find matching closing bracket from this point
				offset := strings.Index(rest2, "[")
				if offset == -1 {
					return nil, fmt.Errorf("malformed array for %s", key)
				}
				startIdx = idx + len(key) + 1 + (len(rest2) - len(strings.TrimLeft(rest2, " \t")))
				// Actually compute absolute start/end in section
				absStart := idx + len(key) + 1 + strings.Index(section[idx+len(key)+1:], "[")
				startIdx = absStart
				// Find ']' that closes the array (naïve but OK for simple lists)
				closeIdx := strings.Index(section[absStart:], "]")
				if closeIdx == -1 {
					return nil, fmt.Errorf("unterminated array for %s", key)
				}
				endIdx = absStart + closeIdx
				break
			}
		}
		idx = strings.Index(section[idx+len(key):], key)
		if idx != -1 {
			idx += len(key)
		}
	}
	if startIdx == -1 || endIdx == -1 {
		// gracefully allow missing = []
		return []string{}, nil
	}
	inside := section[startIdx+1 : endIdx]
	// Split by commas, respect quotes (simple)
	items := splitArrayItems(inside)
	out := make([]string, 0, len(items))
	for _, it := range items {
		s := strings.TrimSpace(it)
		if s == "" {
			continue
		}
		// strip quotes if present
		s = strings.TrimSpace(s)
		n := len(s)
		if n >= 4 && ((s[0] == '"' && s[1] == '"' && s[n-2] == '"' && s[n-1] == '"') ||
			(s[0] == '\'' && s[1] == '\'' && s[n-2] == '\'' && s[n-1] == '\'')) {
			s = s[2 : n-2]
		} else if n >= 2 && ((s[0] == '"' && s[n-1] == '"') || (s[0] == '\'' && s[n-1] == '\'')) {
			s = s[1 : n-1]
		}
		out = append(out, s)
	}
	return out, nil
}

func splitArrayItems(s string) []string {
	var items []string
	var cur strings.Builder
	inS, inD := false, false
	for i := 0; i < len(s); i++ {
		ch := s[i]
		switch ch {
		case '\'':
			if !inD {
				inS = !inS
			}
			cur.WriteByte(ch)
		case '"':
			if !inS {
				inD = !inD
			}
			cur.WriteByte(ch)
		case ',':
			if inS || inD {
				cur.WriteByte(ch)
			} else {
				items = append(items, cur.String())
				cur.Reset()
			}
		default:
			cur.WriteByte(ch)
		}
	}
	if cur.Len() > 0 {
		items = append(items, cur.String())
	}
	return items
}

// ---- File walking and analysis ----

func shouldExclude(path string, excludeAbs []string) bool {
	for _, ex := range excludeAbs {
		if path == ex {
			return true
		}
		// Exclude if under excluded dir
		if strings.HasPrefix(path, ex+string(os.PathSeparator)) {
			return true
		}
	}
	return false
}

func isSourceFile(path string) (string, bool) {
	ext := strings.ToLower(filepath.Ext(path))
	if _, ok := knownExts[ext]; !ok {
		return "", false
	}
	return extToLang[ext], true
}

func countLines(path string) (int, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()

	// Use bufio.Scanner with a large buffer, but also fall back to manual chunk read if needed.
	reader := bufio.NewReader(f)
	lines := 0
	for {
		_, err := reader.ReadString('\n')
		lines++
		if err != nil {
			if errors.Is(err, io.EOF) {
				break
			}
			// If binary file breaks reader, consider 0 lines
			return lines, nil
		}
	}
	return lines, nil
}

func walkAndAnalyze(cfg AnalyzerConfig) ([]FileStat, error) {
	var roots []string
	for _, inc := range cfg.Includes {
		if inc == "" {
			continue
		}
		roots = append(roots, inc)
	}
	if len(roots) == 0 {
		roots = []string{"."}
	}
	// Precompute absolute excludes
	exAbs := make([]string, 0, len(cfg.Excludes))
	for _, ex := range cfg.Excludes {
		abs, _ := filepath.Abs(ex)
		exAbs = append(exAbs, filepath.Clean(abs))
	}

	var results []FileStat

	seen := make(map[string]struct{})
	for _, root := range roots {
		err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				return nil // skip errors, keep going
			}
			abs, _ := filepath.Abs(path)
			if shouldExclude(abs, exAbs) {
				if d.IsDir() {
					return fs.SkipDir
				}
				return nil
			}
			if d.IsDir() {
				return nil
			}
			if _, ok := seen[abs]; ok {
				return nil
			}
			lang, ok := isSourceFile(path)
			if !ok {
				return nil
			}
			info, err := d.Info()
			if err != nil {
				return nil
			}
			n, err := countLines(path)
			if err != nil {
				return nil
			}
			results = append(results, FileStat{
				Path:     path,
				Lang:     lang,
				Lines:    n,
				SizeByte: info.Size(),
			})
			seen[abs] = struct{}{}
			return nil
		})
		if err != nil {
			return results, err
		}
	}
	return results, nil
}

// ---- Stats helpers ----

type Dist struct {
	Count  int
	Sum    int
	Min    int
	Max    int
	Mean   float64
	Median float64
	P10    float64
	P90    float64
}

func computeDist(vals []int) Dist {
	if len(vals) == 0 {
		return Dist{}
	}
	sorted := append([]int(nil), vals...)
	sort.Ints(sorted)
	sum := 0
	for _, v := range sorted {
		sum += v
	}
	min := sorted[0]
	max := sorted[len(sorted)-1]
	mean := float64(sum) / float64(len(sorted))
	median := percentile(sorted, 50)
	p10 := percentile(sorted, 10)
	p90 := percentile(sorted, 90)
	return Dist{
		Count:  len(sorted),
		Sum:    sum,
		Min:    min,
		Max:    max,
		Mean:   mean,
		Median: median,
		P10:    p10,
		P90:    p90,
	}
}

// simple percentile by linear interpolation between ranks
func percentile(sorted []int, p int) float64 {
	if len(sorted) == 0 {
		return 0
	}
	if p <= 0 {
		return float64(sorted[0])
	}
	if p >= 100 {
		return float64(sorted[len(sorted)-1])
	}
	pos := (float64(p) / 100.0) * float64(len(sorted)-1)
	l := int(pos)
	r := l + 1
	if r >= len(sorted) {
		return float64(sorted[l])
	}
	f := pos - float64(l)
	return float64(sorted[l])*(1-f) + float64(sorted[r])*f
}

type Bucket struct {
	Label string
	Lo    int
	Hi    int // inclusive hi; use Hi=-1 to mean open-ended
	Count int
}

func histogram(vals []int) []Bucket {
	bs := []Bucket{
		{"  1–50", 1, 50, 0},
		{" 51–100", 51, 100, 0},
		{"101–200", 101, 200, 0},
		{"201–500", 201, 500, 0},
		{"501–1k", 501, 1000, 0},
		{">  1k", 1001, -1, 0},
	}
	for _, v := range vals {
		for i := range bs {
			if bs[i].Hi == -1 {
				if v >= bs[i].Lo {
					bs[i].Count++
					break
				}
			} else if v >= bs[i].Lo && v <= bs[i].Hi {
				bs[i].Count++
				break
			}
		}
	}
	return bs
}

// ---- Pretty printing ----

func visibleLen(s string) int {
	n := 0
	for i := 0; i < len(s); {
		if s[i] == 0x1b { // ESC
			i++
			if i < len(s) && s[i] == '[' {
				i++
				// consume SGR params until 'm'
				for i < len(s) {
					c := s[i]
					if (c >= '0' && c <= '9') || c == ';' {
						i++
						continue
					}
					if c == 'm' {
						i++
						break
					}
					// unknown terminator -> stop treating as escape
					break
				}
				continue
			}
			// lone ESC visible as one char
			n++
			continue
		}
		_, sz := utf8.DecodeRuneInString(s[i:])
		i += sz
		n++
	}
	return n
}

func padRightANSI(s string, width int) string {
	pad := width - visibleLen(s)
	if pad < 0 {
		pad = 0
	}
	return s + strings.Repeat(" ", pad)
}
func padLeftANSI(s string, width int) string {
	pad := width - visibleLen(s)
	if pad < 0 {
		pad = 0
	}
	return strings.Repeat(" ", pad) + s
}

func hr() {
	fmt.Println(style(strings.Repeat("─", 60), cSubtle))
}

func title(s string) {
	fmt.Println(style("▶ "+s, cHdr, cBold))
}

func kv(k, v string) {
	fmt.Printf("%s: %s\n", style(k, cBold), v)
}

func humanInt(n int) string {
	return style(strconv.FormatInt(int64(n), 10), cNum, cBold)
}
func humanBytes(n int64) string {
	const unit = 1024
	if n < unit {
		return fmt.Sprintf("%d B", n)
	}
	div, exp := unit, 0
	for m := n / unit; m >= unit; m /= unit {
		div *= unit
		exp++
	}
	pre := "KMGTPE"[exp : exp+1]
	return fmt.Sprintf("%.1f %sB", float64(n)/float64(div), pre)
}

func bar(count, max, width int) string {
	if max == 0 || count == 0 {
		return strings.Repeat(" ", width)
	}
	w := int(float64(count) / float64(max) * float64(width))
	if w < 1 {
		w = 1
	}
	filled := style(strings.Repeat("█", w), cOk)
	empty := strings.Repeat(" ", width-w)
	return filled + empty
}

func printOverall(files []FileStat) {
	title("Project Overview")
	kv("Scanned at", time.Now().Format(time.RFC3339))
	kv("Source files", humanInt(len(files)))
	totalBytes := int64(0)
	locs := make([]int, 0, len(files))
	for _, f := range files {
		totalBytes += f.SizeByte
		locs = append(locs, f.Lines)
	}
	dist := computeDist(locs)
	kv("Total lines", humanInt(dist.Sum))
	kv("Lines/file (avg)", fmt.Sprintf("%.2f", dist.Mean))
	kv("Lines/file (median)", fmt.Sprintf("%.2f", dist.Median))
	kv("Lines/file (p10–p90)", fmt.Sprintf("%.0f–%.0f", dist.P10, dist.P90))
	kv("Smallest–Largest file", fmt.Sprintf("%d–%d lines", dist.Min, dist.Max))
	kv("Total size", humanBytes(totalBytes))
	fmt.Println()

	title("Per-file LOC histogram")
	bs := histogram(locs)
	maxC := 0
	for _, b := range bs {
		if b.Count > maxC {
			maxC = b.Count
		}
	}
	const labelW = 7 // fits "501–1k", ">  1k" etc.
	const barW = 28
	for _, b := range bs {
		// Right-align labels so bar start aligns perfectly (handles Unicode “–”)
		lbl := padLeftANSI(style(b.Label, cSubtle), labelW)
		barStr := bar(b.Count, maxC, barW) // already fixed-width
		cnt := style(fmt.Sprintf("(%d)", b.Count), cDim)
		fmt.Printf("  %s %s %s\n", lbl, barStr, cnt)
	}
}

// --- printByLanguage: ANSI-safe column alignment ---
func printByLanguage(files []FileStat) {
	// Aggregate
	agg := map[string]*LangAgg{}
	for _, f := range files {
		entry := agg[f.Lang]
		if entry == nil {
			entry = &LangAgg{}
			agg[f.Lang] = entry
		}
		entry.Files++
		entry.Lines += f.Lines
		entry.Bytes += f.SizeByte
		entry.PerFileLines = append(entry.PerFileLines, f.Lines)
	}
	// Sorting by total lines desc
	type row struct {
		Lang  string
		Files int
		Lines int
		Bytes int64
		Avg   float64
		Med   float64
	}
	rows := make([]row, 0, len(agg))
	for lang, a := range agg {
		d := computeDist(a.PerFileLines)
		rows = append(rows, row{
			Lang:  lang,
			Files: a.Files,
			Lines: a.Lines,
			Bytes: a.Bytes,
			Avg:   d.Mean,
			Med:   d.Median,
		})
	}
	sort.Slice(rows, func(i, j int) bool { return rows[i].Lines > rows[j].Lines })

	// Compute column widths (visible chars, no ANSI)
	langW := len("Language")
	filesW := len("Files")
	linesW := len("Lines")
	avgW := len("Avg/file")
	sizeW := len("Size")
	for _, r := range rows {
		if l := len(r.Lang); l > langW {
			langW = l
		}
		if l := len(strconv.Itoa(r.Files)); l > filesW {
			filesW = l
		}
		if l := len(strconv.Itoa(r.Lines)); l > linesW {
			linesW = l
		}
		if l := len(fmt.Sprintf("%.2f", r.Avg)); l > avgW {
			avgW = l
		}
		if l := len(humanBytes(r.Bytes)); l > sizeW {
			sizeW = l
		}
	}

	fmt.Println()
	title("Language Distribution")
	hr()
	// Header
	hLang := padRightANSI(style("Language", cBold), langW)
	hFiles := padLeftANSI(style("Files", cBold), filesW)
	hLines := padLeftANSI(style("Lines", cBold), linesW)
	hAvg := padLeftANSI(style("Avg/file", cBold), avgW)
	hSize := padLeftANSI(style("Size", cBold), sizeW)
	fmt.Printf("%s  %s  %s  %s  %s\n", hLang, hFiles, hLines, hAvg, hSize)
	hr()

	// Rows
	for _, r := range rows {
		lang := padRightANSI(style(r.Lang, cLang, cBold), langW)
		files := padLeftANSI(humanInt(r.Files), filesW)
		lines := padLeftANSI(humanInt(r.Lines), linesW)
		avg := padLeftANSI(fmt.Sprintf("%.2f", r.Avg), avgW)
		size := padLeftANSI(humanBytes(r.Bytes), sizeW)
		fmt.Printf("%s  %s  %s  %s  %s\n", lang, files, lines, avg, size)
	}
	hr()
}

func printTail(files []FileStat, limit int) {
	if len(files) == 0 {
		return
	}
	// Show a few largest files by LOC
	type fr struct {
		Path  string
		Lang  string
		Lines int
	}
	arr := make([]fr, 0, len(files))
	for _, f := range files {
		arr = append(arr, fr{Path: f.Path, Lang: f.Lang, Lines: f.Lines})
	}
	sort.Slice(arr, func(i, j int) bool { return arr[i].Lines > arr[j].Lines })
	if limit > len(arr) {
		limit = len(arr)
	}

	// Compute widths (ANSI-aware)
	langW := len("Lang")
	pathW := len("Path")
	linesW := len("Lines")
	for _, r := range arr[:limit] {
		if l := len(r.Lang); l > langW {
			langW = l
		}
		if l := len(r.Path); l > pathW {
			pathW = l
		}
		if l := len(strconv.Itoa(r.Lines)); l > linesW {
			linesW = l
		}
	}

	fmt.Println()
	title(fmt.Sprintf("Top %d Largest Files (by LOC)", limit))
	for i := 0; i < limit; i++ {
		lang := padRightANSI(style(arr[i].Lang, cLang), langW)
		path := padRightANSI(style(arr[i].Path, cPath), pathW)
		lines := padLeftANSI(style(strconv.Itoa(arr[i].Lines), cNum, cBold), linesW)
		fmt.Printf("  %2d. %s  %s  %s lines\n", i+1, lang, path, lines)
	}
}

// ---- main ----

func usage() {
	fmt.Fprintf(os.Stderr, "Usage: %s <path/to/analyzer_config.toml>\n", filepath.Base(os.Args[0]))
	os.Exit(2)
}

func main() {
	if len(os.Args) != 2 {
		usage()
	}
	cfgPath := os.Args[1]
	cfg, err := parseConfig(cfgPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, style("Config error: "+err.Error(), cBad, cBold))
		os.Exit(1)
	}

	files, err := walkAndAnalyze(cfg)
	if err != nil {
		fmt.Fprintln(os.Stderr, style("Scan error: "+err.Error(), cBad, cBold))
		os.Exit(1)
	}
	if len(files) == 0 {
		fmt.Println(style("No source files found under includes (after excludes).", cWarn))
		return
	}

	// Output
	fmt.Println(style("┌────────────────────────────┐", cSubtle))
	fmt.Println(style("│  Source Analysis Summary   │", cSubtle, cBold))
	fmt.Println(style("└────────────────────────────┘", cSubtle))
	printOverall(files)
	printByLanguage(files)
	printTail(files, 10)

	fmt.Println()
	fmt.Println(style("Done.", cOk, cBold))
}
