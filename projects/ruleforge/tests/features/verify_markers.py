"""Property markers preserve visibility, variant precedence and purpose styling."""
import json,subprocess,tempfile,sys
from pathlib import Path
engine=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="ruleforge-marker-") as d:
 p=Path(d)
 (p/"entry.rf").write_text('''version 1.0.0;
module entry;
import std.conditions;
import std.rules;
themeSchema Labels { low; high; marker; }
theme T implements Labels {
 low { onShow { Text #AAAAAA; Border #111111; Background #123456; FontSize 28; MinimapIcon 2, Grey, Moon; } }
 high { onShow { Text #FFFFFF; Border #FFFFFF; Background #654321; FontSize 45; MinimapIcon 0, White, Diamond; CustomSound "big.mp3", 250; } }
 marker { onShow { Text #CCFFFF; Border #FF00FF; FontSize 33; MinimapIcon 2, Cyan, Triangle; CustomSound "click.mp3", 120; } }
}
func Policy() -> ruleset {
 rule Protected { scope { baseType = "Protected"; } baseline { action hide; } }
 rule Budget { scope { baseType = "Budget"; } baseline { action hide; style T.low; } variants { when rarity == "Rare" { action show; } } }
 rule Wanted { scope { baseType = "Wanted"; } baseline { action show; style T.low; } }
 rule High { scope { baseType = "High"; } baseline { action show; style T.high; } }
 rule Weapon { scope { class = "Wands"; } baseline { action show; style T.low; } }
 rule Rest { baseline { action hide; } }
}
func Marker() -> ruleset {
 rule Perfect { scope { class = "Boots"; baseType = ["Protected", "Budget", "Wanted", "High", "Missing"]; baseDefencePercentile = 100; } baseline { action show; style T.marker; } }
}
export func main() { include rules.MarkShown(Policy(), Marker()); }
''')
 samples=[dict(baseType=name,**{"class":kind},rarity=rarity,baseDefencePercentile=pct) for name,kind in [("Protected","Boots"),("Budget","Boots"),("Wanted","Boots"),("High","Boots"),("Weapon","Wands"),("Missing","Boots")] for rarity in ["Normal","Rare"] for pct in [99,100]]
 (p/"items.json").write_text(json.dumps(samples))
 subprocess.run([str(engine),"compile","entry.rf","-o","out.filter","--model","model.json"],cwd=p,check=True)
 raw=subprocess.check_output([str(engine),"simulate","entry.rf","--items","items.json"],cwd=p,text=True)
 results=json.loads(raw);model=json.loads((p/"model.json").read_text());blocks={x["id"]:x for x in model["blocks"]}

 for s,r in zip(samples,results,strict=True):
  want="hide" if s["baseType"] in ["Protected","Missing"] or (s["baseType"]=="Budget" and s["rarity"]=="Normal") else "show"
  assert r["action"]==want,(s,r)
  if want=="show":
   props={x["keyword"]:x["value"] for x in r["style"]["properties"]}
   marked=s["baseDefencePercentile"]==100 and s["class"]=="Boots"
   assert props["SetBorderColor"]==("255 0 255" if marked else "255 255 255" if s["baseType"]=="High" else "17 17 17"),(s,props)
   assert props["SetFontSize"]==("45" if s["baseType"]=="High" else "33" if marked else "28"),(s,props)
   assert props["SetBackgroundColor"]==("101 67 33 255" if s["baseType"]=="High" else "18 52 86 255")
   assert props["MinimapIcon"]==("0 White Diamond" if s["baseType"]=="High" else "2 Cyan Moon" if marked else "2 Grey Moon")
   if s["baseType"]=="High":assert props["CustomAlertSound"]=='"big.mp3" 250'
   elif marked:assert props["CustomAlertSound"]=='"click.mp3" 120'
 lines=(p/"out.filter").read_text().splitlines()
 for block in model["blocks"]:
  assert lines[block["lines"][0]-1] in ["Show","Hide"]
  assert lines[block["lines"][1]-1].strip()
  keys=[x["keyword"] for x in block["conditions"]]
  assert keys.count("Class")<=1 and keys.count("BaseType")<=1,keys
 source=(p/"entry.rf").read_text()
 (p/"entry.rf").write_text(source.replace("action show; style T.marker;", "action hide; style T.marker;"))
 invalid=subprocess.run([str(engine),"compile","entry.rf"],cwd=p,text=True,capture_output=True)
 assert invalid.returncode and "RF0525" in invalid.stderr,invalid
 print("PASS: markers preserve first-match visibility, variant actions, priority, background and icon shape")
