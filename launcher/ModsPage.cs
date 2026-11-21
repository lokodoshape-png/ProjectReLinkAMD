using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using static ReLink.Texts;

namespace ReLink;

/// (relink) Home's cards past the two settings ones (Mods, What's new), the update banner, and the Mods page: what the game
/// folder's mods hold, each mod on or off by its folder's name (a leading '-' turns it off, as the game and the build read
/// it: host/mods.c, nx2/exefs_mods.py), the code patches a build left without effect, and a build when they changed
public partial class MainWindow
{
    record ModEntry(string Path, string Name, bool On, bool RomFs, bool ExeFs, bool Repair);

    /// The mods below the folder's mods: any folder (four levels at most) holding a romfs or exefs folder is one; a name
    /// starting with '-', '_' or '.' is off (the folder itself; a folder off above it hides those below, as the game skips them)
    static List<ModEntry> FindMods(string folder)
    {
        var list = new List<ModEntry>();
        if (folder == "") return list;
        var root = ExefsMods.Root(folder);
        if (!Directory.Exists(root)) return list;
        void Walk(string dir, int depth)
        {
            if (depth > 4) return;
            IEnumerable<string> subs;
            try { subs = Directory.EnumerateDirectories(dir).OrderBy(d => d, StringComparer.OrdinalIgnoreCase).ToList(); }
            catch (Exception) { return; }
            foreach (var sub in subs)
            {
                var name = Path.GetFileName(sub);
                bool romfs = Directory.Exists(Path.Combine(sub, "romfs")), exefs = Directory.Exists(Path.Combine(sub, "exefs"));
                if (romfs || exefs)
                {
                    var on = !(name.StartsWith('-') || name.StartsWith('_') || name.StartsWith('.'));
                    var shown = on ? name : name[1..];
                    list.Add(new ModEntry(sub, shown, on, romfs, exefs, shown.Equals("00-dump-repair", StringComparison.OrdinalIgnoreCase)));
                }
                else if (!(name.StartsWith('-') || name.StartsWith('_') || name.StartsWith('.'))) Walk(sub, depth + 1);
            }
        }
        Walk(root, 1);
        return list;
    }

    /// The lines of relink_exefs_mods.txt for the patches the last build did not apply or that change nothing here
    static List<string> PatchProblems(string folder)
    {
        var lines = new List<string>();
        try
        {
            foreach (var line in File.ReadLines(Path.Combine(folder, ExefsMods.Report)).Skip(1))
                if (line.StartsWith("left out") || line.StartsWith("no effect") || line.StartsWith("not translated") || line.StartsWith("conflict"))
                    lines.Add(line.Trim());
        }
        catch (Exception) { }
        return lines;
    }

    /// Home's Mods and What's new cards, from the folder as it is now
    void UpdateHomeCards()
    {
        var mods = FindMods(s.Folder);
        var on = mods.Count(m => m.On && !m.Repair);
        var problems = s.Folder != "" ? PatchProblems(s.Folder).Count : 0;
        var names = mods.Where(m => m.On && !m.Repair).Select(m => m.Name).Take(2).ToList();
        ModsSub.Text = names.Count > 0 ? string.Join(", ", names) + (on > 2 ? "…" : "")
                     : L("Textures, modèles, sons, patchs de code", "Textures, models, sounds, code patches");
        ModsCardText.Text = on == 0 ? L("Aucun mod actif", "No mods on")
                          : on == 1 ? L("1 mod actif", "1 mod on") : L($"{on} mods actifs", $"{on} mods on");
        if (modsToBuild) ModsCardText.Text += L(" · à reconstruire", " · to build");
        else if (problems > 0) ModsCardText.Text += L($" · {problems} sans effet", $" · {problems} without effect");
        // (the version is the side's card: here, only where to read what changed)
        NewsText.Text = L("Notes de version", "Release notes");
        NewsSub.Text = L("Ce qui a changé, sur GitHub", "What changed, on GitHub");
    }

    void ModsCardClick(object sender, MouseButtonEventArgs e) { e.Handled = true; NavMods.IsChecked = true; }

    void NewsCardClick(object sender, MouseButtonEventArgs e)
    {
        e.Handled = true;
        var version = s.Folder != "" ? Game.Version(s.Folder) : null;
        Open(version != null ? $"https://github.com/lokodoshape-png/ProjectReLinkAMD/releases/tag/{version}" : "https://github.com/lokodoshape-png/ProjectReLinkAMD/releases");
    }

    void OpenModsFolder(object sender, RoutedEventArgs e)
    {
        if (s.Folder == "") return;
        var root = ExefsMods.Root(s.Folder);
        try { Directory.CreateDirectory(root); } catch (Exception) { }
        Open(root);
    }

    /// The Mods page: a row a mod, with what kind it is and a switch; the code patches without effect under them
    void ShowModsPage(bool preview = false)
    {
        ModsList.Children.Clear();
        var mods = preview
            ? new List<ModEntry>
              {
                  new("", "00-dump-repair", true, true, false, true), new("", "HD Textures", true, true, false, false),
                  new("", "Randomizer", false, false, true, false), new("", "60fps", true, false, true, false),
              }
            : FindMods(s.Folder);
        if (mods.Count == 0)
            ModsList.Children.Add(new TextBlock
            {
                Text = L("Aucun mod. Mettez un mod dans le dossier mods du jeu : mods\\<nom>\\romfs pour les textures, modèles et sons, mods\\<nom>\\exefs pour les patchs de code.",
                         "No mods yet. Put one in the game folder's mods: mods\\<name>\\romfs for textures, models and sounds, mods\\<name>\\exefs for code patches."),
                Style = St("Body"), Margin = new Thickness(2, 6, 0, 0),
            });
        foreach (var m in mods)
        {
            var kind = string.Join(" + ", new[] { m.RomFs ? "romfs" : null, m.ExeFs ? "exefs" : null }.Where(k => k != null));
            var hint = m.Repair ? L("Réparation automatique de votre dump, faite à chaque installation.", "The automatic repair of your dump, made at every install.")
                     : m.ExeFs ? L("Patchs de code : compte après une reconstruction du jeu.", "Code patches: count once the game is built again.")
                     : L("Compte dès le prochain lancement.", "Counts from the next start.");
            var sw = new CheckBox { Style = St("Switch"), IsChecked = m.On, VerticalAlignment = VerticalAlignment.Center, IsEnabled = !preview && game == null && runner == null };
            var entry = m;
            sw.Click += (_, _) => ToggleMod(entry, sw);
            var label = new StackPanel
            {
                Children =
                {
                    new TextBlock { Text = m.Name, Style = St("RowLabel") },
                    new TextBlock { Text = $"{kind} · {hint}", Style = St("RowHint"), TextWrapping = TextWrapping.Wrap },
                },
            };
            var row = new Grid { ColumnDefinitions = { new ColumnDefinition(), new ColumnDefinition { Width = GridLength.Auto } } };
            row.Children.Add(label);
            Grid.SetColumn(sw, 1);
            row.Children.Add(sw);
            ModsList.Children.Add(new Border { Style = St("Row"), Child = row });
        }
        var problems = preview ? new List<string> { "left out  Randomizer/exefs/1A2B3C.ips: made for the module 1A2B3C, which is not one of this game's (a mod for another version of the game)" }
                               : s.Folder != "" ? PatchProblems(s.Folder) : new List<string>();
        if (problems.Count > 0)
        {
            ModsList.Children.Add(new TextBlock { Text = L("Patchs de code sans effet à la dernière construction", "Code patches without effect at the last build"), Style = St("Section") });
            foreach (var p in problems.Take(8))
                ModsList.Children.Add(new TextBlock { Text = p, Style = St("RowHint"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(2, 2, 0, 2) });
        }
        ModsRebuild.Visibility = !preview && runner == null && s.Folder != "" && Game.Installed(s.Folder) && Game.KeepsGameFiles(s.Folder) &&
                                 ExefsMods.Changed(s.Folder, out _) ? Visibility.Visible : Visibility.Collapsed;
    }

    /// A mod on or off: its folder renamed with or without a leading '-' (not while the game runs: it reads them)
    void ToggleMod(ModEntry m, CheckBox sw)
    {
        if (game != null || runner != null || m.Path == "") { sw.IsChecked = m.On; return; }
        var dir = Path.GetDirectoryName(m.Path)!;
        var target = Path.Combine(dir, sw.IsChecked == true ? m.Name : "-" + m.Name);
        try
        {
            if (Directory.Exists(target)) throw new IOException(L("un dossier de ce nom existe déjà", "a folder with that name exists already"));
            Directory.Move(m.Path, target);
        }
        catch (Exception ex)
        {
            sw.IsChecked = m.On;
            MainNote.Text = L("Mod non changé : ", "Mod not changed: ") + ex.Message;
            return;
        }
        ShowModsPage();
        UpdateHomeCards();
        ShowMods();
    }
}
