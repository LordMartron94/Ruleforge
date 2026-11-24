// Command ruleforge-studio is the host around the Ruleforge engine: a web editor and inspector, a scheduler that refreshes
// data and republishes filters, and the self-updater.
//
//	ruleforge-studio [serve] [--project DIR] [--addr 127.0.0.1:8750]
//	ruleforge-studio build   [--project DIR]     refresh data, compile every output once, publish
//	ruleforge-studio init DIR                    create a starter project
//	ruleforge-studio update  [--apply]           check (and install) the latest release
//	ruleforge-studio version
package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"time"

	"github.com/LordMartron94/Ruleforge/studio/internal/config"
	"github.com/LordMartron94/Ruleforge/studio/internal/engine"
	"github.com/LordMartron94/Ruleforge/studio/internal/logging"
	"github.com/LordMartron94/Ruleforge/studio/internal/regen"
	"github.com/LordMartron94/Ruleforge/studio/internal/server"
	"github.com/LordMartron94/Ruleforge/studio/internal/update"
)

// version is set at build time: -ldflags "-X main.version=1.2.3".
var version = "dev"

// repository is the public GitHub repository releases are published to and updates are read from.
const repository = "LordMartron94/Ruleforge"

func main() {
	args := os.Args[1:]
	command := "serve"
	if len(args) > 0 && len(args[0]) > 0 && args[0][0] != '-' {
		command, args = args[0], args[1:]
	}
	switch command {
	case "serve":
		os.Exit(serve(args))
	case "build":
		os.Exit(build(args))
	case "init":
		os.Exit(initialize(args))
	case "update":
		os.Exit(runUpdate(args))
	case "version":
		fmt.Println("ruleforge-studio", version)
	default:
		fmt.Fprintf(os.Stderr, "unknown command %q\n", command)
		os.Exit(2)
	}
}

func installDir() string {
	self, err := os.Executable()
	if err != nil {
		return "."
	}
	return filepath.Dir(self)
}

func newUpdater() *update.Updater {
	return &update.Updater{Repo: repository, Current: version, InstallDir: installDir()}
}

func openProject(dir, enginePath string) (*config.Project, engine.Engine, error) {
	absolute, err := filepath.Abs(dir)
	if err != nil {
		return nil, engine.Engine{}, err
	}
	project, err := config.Load(absolute)
	if err != nil {
		return nil, engine.Engine{}, err
	}
	eng, err := engine.Locate(enginePath)
	if err != nil {
		return nil, engine.Engine{}, err
	}
	eng.LogDir, eng.LogKeep, _ = project.LogOptions()
	return project, eng, nil
}

func serve(args []string) int {
	flags := flag.NewFlagSet("serve", flag.ExitOnError)
	dir := flags.String("project", ".", "the project folder")
	addr := flags.String("addr", "127.0.0.1:8750", "the address to listen on (loopback only by default)")
	enginePath := flags.String("engine", "", "the ruleforge executable")
	noUpdate := flags.Bool("no-update-check", false, "do not look for a newer release")
	_ = flags.Parse(args)
	project, eng, err := openProject(*dir, *enginePath)
	if err != nil {
		log.Print(err)
		return 1
	}
	logDir, keep, size := project.LogOptions()
	writer, err := logging.Open(logDir, keep, size)
	if err != nil {
		log.Print(err)
		return 1
	}
	defer writer.Close()
	log.SetOutput(io.MultiWriter(os.Stderr, writer))
	defer log.SetOutput(os.Stderr)
	var updater *update.Updater
	if !*noUpdate {
		updater = newUpdater()
	}
	app := server.New(project, eng, version, updater)
	regenerator := &regen.Regenerator{Project: project, Engine: eng, Log: log.Printf}
	app.RegenerationHooks(regenerator.Status, func(ctx context.Context) error { return regenerator.RunOnce(ctx, true) })
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	if project.Regeneration.Enabled {
		go regenerator.Loop(ctx)
	}
	listener, err := net.Listen("tcp", *addr)
	if err != nil {
		log.Print(err)
		return 1
	}
	httpServer := &http.Server{Handler: app.Handler(), ReadHeaderTimeout: 10 * time.Second}
	go func() {
		<-ctx.Done()
		shutdown, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		_ = httpServer.Shutdown(shutdown)
	}()
	log.Printf("Ruleforge Studio %s: project %s, open http://%s", version, project.Dir, listener.Addr())
	if err := httpServer.Serve(listener); err != nil && err != http.ErrServerClosed {
		log.Print(err)
		return 1
	}
	return 0
}

func build(args []string) int {
	flags := flag.NewFlagSet("build", flag.ExitOnError)
	dir := flags.String("project", ".", "the project folder")
	enginePath := flags.String("engine", "", "the ruleforge executable")
	_ = flags.Parse(args)
	project, eng, err := openProject(*dir, *enginePath)
	if err != nil {
		log.Print(err)
		return 1
	}
	logDir, keep, size := project.LogOptions()
	writer, err := logging.Open(logDir, keep, size)
	if err != nil {
		log.Print(err)
		return 1
	}
	defer writer.Close()
	log.SetOutput(io.MultiWriter(os.Stderr, writer))
	defer log.SetOutput(os.Stderr)
	regenerator := &regen.Regenerator{Project: project, Engine: eng, Log: log.Printf}
	if err := regenerator.RunOnce(context.Background(), true); err != nil {
		log.Print(err)
		return 1
	}
	for _, output := range regenerator.Status().(regen.Status).Outputs {
		fmt.Printf("%s -> %s (%d blocks)\n", output.Name, output.Path, output.Blocks)
	}
	return 0
}

const starterEntry = `version 1.0.0;
module entry;

import filters.themes;

configuration {
    default-theme = themes.DefaultTheme;
}

export func main() {
    emit rule {
        scope { class = "Currency"; baseType = "Exalted Orb"; }
        action show;
        style godTier;
    }
}
`

const starterThemes = `version 1.0.0;
module filters.themes;

export themeSchema BaseTiering {
    godTier;
}

export theme DefaultTheme implements BaseTiering {
    godTier {
        onShow {
            Background #FFD700;
            FontSize 45;
        }
    }
}
`

func initialize(args []string) int {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "usage: ruleforge-studio init DIR")
		return 2
	}
	dir := args[0]
	files := map[string]string{
		"entry.rf":          starterEntry,
		"filters/themes.rf": starterThemes,
		"studio.json":       "{\n  \"entry\": \"entry.rf\",\n  \"outputs\": [{\"name\": \"My filter\", \"path\": \"out/my.filter\"}],\n  \"regeneration\": {\"enabled\": false, \"preserveLastValid\": true}\n}\n",
	}
	for name, content := range files {
		path := filepath.Join(dir, filepath.FromSlash(name))
		if _, err := os.Stat(path); err == nil {
			fmt.Fprintf(os.Stderr, "%s exists; not overwriting\n", path)
			return 1
		}
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			fmt.Fprintln(os.Stderr, err)
			return 1
		}
		if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
			fmt.Fprintln(os.Stderr, err)
			return 1
		}
	}
	fmt.Println("created", dir)
	return 0
}

func runUpdate(args []string) int {
	flags := flag.NewFlagSet("update", flag.ExitOnError)
	apply := flags.Bool("apply", false, "install the latest release if it is newer")
	_ = flags.Parse(args)
	updater := newUpdater()
	release, available, err := updater.Check(context.Background())
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		return 1
	}
	if !available {
		fmt.Printf("up to date (%s; latest release %s)\n", version, release.Version)
		return 0
	}
	fmt.Printf("update available: %s -> %s\n", version, release.Version)
	if !*apply {
		fmt.Println("run `ruleforge-studio update --apply` to install it")
		return 0
	}
	if err := updater.Apply(context.Background(), release); err != nil {
		fmt.Fprintln(os.Stderr, err)
		return 1
	}
	fmt.Println("installed", release.Version, "- restart to use it")
	return 0
}
