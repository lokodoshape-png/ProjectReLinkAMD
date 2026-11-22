using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;
using static ReLink.Texts;

namespace ReLink;

/// The install and the update: the player's copy of the game is chosen and checked, nx2/make_game.py builds the game folder
/// from it (Runner.cs), and the page follows its steps. An update (Updater.cs) uses the same page with one more step.
public partial class MainWindow
{
    readonly DispatcherTimer clock = new() { Interval = TimeSpan.FromSeconds(1) };
    readonly Stopwatch elapsed = new();
    readonly Stopwatch compileClock = new();
    readonly Dictionary<string, StepRow> rows = new();
    readonly Dictionary<string, (bool Ok, string Text)?> checks = new(StringComparer.OrdinalIgnoreCase);
    readonly List<Dump> dumps = new();
    readonly System.Text.StringBuilder log = new();
    Runner? runner;
    string? current;
    string finished = "";
    string outPath = Settings.DefaultFolder;
    string? chosenNup, romfs, exefs, foldersNote;
    bool updating, checking, launcherIsOld;
    double compiled;
    int filesDone, filesOf, filesChanged = -1, filesCompiled;
    double compileEta = -1;
    long thisCompile, previousTotal = -1;
    string lastFailure = "";
    string? latestTag, updateTarget;

    // ---- what the player chose last time (the install folder, the game files): install.json in ReLink's folder ----
    sealed class InstallChoices
    {
        public string? Out { get; set; }
        public string? Nup { get; set; }
        public string? Romfs { get; set; }
        public string? Exefs { get; set; }
    }
    static string ChoicesPath => Path.Combine(Storage.Base, "install.json");

    void LoadInstallChoices()
    {
        InstallChoices? c = null;
        try { c = System.Text.Json.JsonSerializer.Deserialize<InstallChoices>(File.ReadAllText(ChoicesPath)); } catch (Exception) { }
        if (s.Folder != "") outPath = s.Folder;
        if (c == null) return;
        if (s.Folder == "" && !string.IsNullOrEmpty(c.Out)) outPath = c.Out;
        if (c.Romfs != null && c.Exefs != null && Directory.Exists(c.Romfs) && File.Exists(Path.Combine(c.Exefs, "main")))
        {
            romfs = c.Romfs;
            exefs = c.Exefs;
            foldersNote = L($"Dossiers utilisés : {romfs} et {exefs} (comme la dernière fois)", $"Using the folders {romfs} and {exefs} (as last time)", $"Usando as pastas {romfs} e {exefs} (como da última vez)");
        }
        else if (c.Nup != null && File.Exists(c.Nup))
        {
            var info = new FileInfo(c.Nup);
            dumps.Insert(0, new Dump(info.FullName, L("La dernière fois", "Last time", "Última vez"), info.Length, info.LastWriteTime));
            chosenNup = c.Nup;
        }
    }

    void SaveInstallChoices()
    {
        try
        {
            var c = new InstallChoices { Out = outPath, Nup = romfs == null ? chosenNup : null, Romfs = romfs, Exefs = exefs };
            Directory.CreateDirectory(Storage.Base);
            File.WriteAllText(ChoicesPath, System.Text.Json.JsonSerializer.Serialize(c));
        }
        catch (Exception) { }
    }

    // ================= 1: the player's copy =================

    void ShowInstall()
    {
        updating = false;
        SetStepNotes(update: false);
        Show(View.Install);
        var tools = Tools.Find();
        ToolsNote.Visibility = tools == null ? Visibility.Visible : Visibility.Collapsed;
        ToolsNote.Text = L("Les outils de ReLink manquent dans son dossier. Lancez de nouveau ReLink-Setup.exe : il les télécharge et les vérifie.",
                           "ReLink's build tools are missing from its folder. Run ReLink-Setup.exe again: it downloads and checks them.", "As ferramentas de compilação do ReLink estão faltando na pasta dele. Rode o ReLink-Setup.exe de novo: ele baixa e verifica tudo.");
        FillDumps();
        Refresh();
    }

    void SetStepNotes(bool update)
    {
        var steps = new List<(string, string)>
        {
            ("read", L("Lecture", "Read", "Ler")), ("lift", L("Traduction", "Translate", "Traduzir")), ("compile", L("Compilation", "Compile", "Compilar")), ("finish", L("Assemblage", "Assemble", "Montar")),
        };
        if (update) steps.Insert(0, ("download", L("Réception", "Download", "Baixar")));
        staff.Set(steps.ToArray());
    }

    void FillDumps()
    {
        DumpList.Children.Clear();
        if (romfs != null && exefs != null && foldersNote != null)
            DumpList.Children.Add(new TextBlock { Text = foldersNote, Style = St("Body"), Foreground = Res("Bone"), Margin = new Thickness(0, 4, 0, 8) });
        if (dumps.Count == 0)
        {
            if (romfs == null)
                DumpList.Children.Add(new TextBlock
                {
                    Text = L("Aucun fichier .nup trouvé dans Téléchargements, Bureau ou Documents. Choisissez le vôtre ci-dessous, ou déposez-le sur cette fenêtre.",
                             "No .nup file found in Downloads, Desktop or Documents. Choose yours below, or drop it on this window.", "Nenhum arquivo .nup encontrado em Downloads, Área de Trabalho ou Documentos. Escolha o seu abaixo, ou solte-o nesta janela."),
                    Style = St("Body"), Margin = new Thickness(0, 4, 0, 4),
                });
            return;
        }
        foreach (var d in dumps)
        {
            checks.TryGetValue(d.Path, out var c);
            var (state, brush) = !checks.ContainsKey(d.Path) || c == null ? (L("Vérification…", "Checking…", "Verificando…"), "BoneFaint")
                : c.Value.Ok ? (L("Bonne version", "Supported version", "Versão suportada"), "Tunic")
                : c.Value.Text.StartsWith("This is build") ? (L("Autre version du jeu", "Not the supported version", "Não é a versão suportada"), "Warn")
                : (L("Illisible", "Can't be read", "Não dá para ler"), "Warn");
            var name = new TextBlock { Text = Path.GetFileName(d.Path), FontSize = 15, TextTrimming = TextTrimming.CharacterEllipsis };
            var meta = new TextBlock { Text = $"{d.Where}, {Bytes(d.Size)}, {Finder.Ago(d.Modified)}", FontSize = 12.5, Foreground = Res("BoneFaint"), Margin = new Thickness(0, 2, 0, 0) };
            var texts = new StackPanel { Children = { name, meta } };
            // (why a file is refused is said in its row, not only in a tooltip)
            if (c is { Ok: false })
                texts.Children.Add(new TextBlock { Text = Why(c.Value.Text), FontSize = 12.5, Foreground = Res("Warn"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 3, 0, 0) });
            var chip = new TextBlock { Text = state, FontSize = 13, Foreground = Res(brush), VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(14, 2, 0, 0) };
            var grid = new Grid { ColumnDefinitions = { new ColumnDefinition(), new ColumnDefinition { Width = GridLength.Auto } } };
            Grid.SetColumn(chip, 1);
            grid.Children.Add(texts);
            grid.Children.Add(chip);
            var row = new RadioButton
            {
                Style = St("FileRow"), GroupName = "Dumps", Content = grid, ToolTip = d.Path,
                IsChecked = romfs == null && string.Equals(d.Path, chosenNup, StringComparison.OrdinalIgnoreCase),
            };
            row.Checked += (_, _) => Choose(d.Path);
            DumpList.Children.Add(row);
        }
    }

    /// The tools' own words for a refused file, in the launcher's language when it is the usual reason (another version of the game)
    static string Why(string text)
    {
        var m = Regex.Match(text, @"^This is build (\w+)\. \w+ supports build (\w+)\.");
        return m.Success ? L($"C'est la version {m.Groups[1].Value} du jeu ; ReLink prend en charge la version {m.Groups[2].Value}.",
                             $"This is build {m.Groups[1].Value} of the game; ReLink supports build {m.Groups[2].Value}.", $"Esta é a build {m.Groups[1].Value} do jogo; o ReLink suporta a build {m.Groups[2].Value}.") : text;
    }

    /// Each found file's version, one at a time (the check unpacks into one probe folder); the first good one is chosen
    async void CheckAll()
    {
        if (checking) return;
        checking = true;
        var tools = Tools.Find();
        while (tools != null && dumps.FirstOrDefault(d => !checks.ContainsKey(d.Path)) is { } d)
        {
            checks[d.Path] = null;
            FillDumps();
            var r = await Runner.Check(tools, new[] { "--check", "--nup", d.Path });
            checks[d.Path] = r;
            if (!r.Ok && r.Text.Contains("antivirus", StringComparison.OrdinalIgnoreCase))
            {
                checking = false;
                ShowProblem(r.Text, tools: true);
                return;
            }
            if (chosenNup == null && romfs == null && r.Ok) chosenNup = d.Path;
            FillDumps();
            Refresh();
        }
        if (chosenNup == null && romfs == null && dumps.Count > 0 && tools != null) { chosenNup = dumps[0].Path; FillDumps(); }
        checking = false;
        Refresh();
    }

    void Choose(string path)
    {
        chosenNup = path;
        if (romfs != null)
        {
            romfs = exefs = null;
            Dispatcher.BeginInvoke(FillDumps);   // (the folders' line goes; not from inside this row's own event)
        }
        Refresh();
    }

    void AddDump(string path, string where)
    {
        var info = new FileInfo(path);
        if (!dumps.Any(d => string.Equals(d.Path, info.FullName, StringComparison.OrdinalIgnoreCase)))
            dumps.Insert(0, new Dump(info.FullName, where, info.Length, info.LastWriteTime));
        chosenNup = info.FullName;
        romfs = exefs = null;
        FillDumps();
        CheckAll();
        Refresh();
    }

    bool SourceReady => romfs != null && exefs != null
        || chosenNup != null && checks.TryGetValue(chosenNup, out var c) && c is { Ok: true };

    /// The install page's own numbers and its button: the free space, and what pressing the button would do
    void Refresh()
    {
        OutPathText.Text = outPath;
        var free = Storage.FreeOn(outPath);
        string disk;
        try { disk = (Path.GetPathRoot(Path.GetFullPath(outPath)) ?? "").TrimEnd('\\'); } catch (Exception) { disk = ""; }
        var gb = Storage.NeededBytes >> 30;
        SpaceText.Text = free < 0 ? L($"Il faut {gb} Go libres pendant l'installation.", $"The install needs {gb} GB free while it works.", $"A instalação precisa de {gb} GB livres enquanto trabalha.")
            : L($"Il faut {gb} Go libres pendant l'installation ; {disk} en a {free >> 30}.", $"The install needs {gb} GB free while it works; {disk} has {free >> 30}.", $"A instalação precisa de {gb} GB livres enquanto trabalha; {disk} tem {free >> 30}.");
        SpaceText.Foreground = Res(free >= 0 && free < Storage.NeededBytes ? "Warn" : "BoneFaint");
        RefreshMode();
    }

    void RefreshMode()
    {
        if (view != View.Install || updating || runner != null) return;
        var mine = Tools.Find()?.Version;
        launcherIsOld = latestTag != null && mine != null && mine != latestTag;
        if (launcherIsOld)
        {
            MainButtonText.Text = L("Mettre ReLink à jour", "Update ReLink", "Atualizar o ReLink");
            MainButton.IsEnabled = true;
            Card(L("Installation", "Install", "Instalar"), L($"{latestTag} dispo", $"{latestTag} out", $"{latestTag} disponível"), Pill.Gold);
            MainNote.Text = L("Mettez ReLink à jour avant d'installer.", "Update ReLink before installing.", "Atualize o ReLink antes de instalar.");
            return;
        }
        Card(L("Installation", "Install", "Instalar"));
        MainButtonText.Text = Game.Installed(outPath) ? L("Réinstaller", "Install again", "Instalar de novo") : L("Installer", "Install", "Instalar");
        MainButton.IsEnabled = SourceReady && outPath.Length > 2 && Tools.Find() != null;
        MainNote.Text = Tools.Find() == null ? L("Les outils de ReLink manquent.", "ReLink's build tools are missing.", "As ferramentas de compilação do ReLink estão faltando.")
            : SourceReady ? L("Tout est prêt.", "Everything is ready.", "Tudo pronto.")
            : L("Choisissez votre copie du jeu.", "Choose your copy of the game.", "Escolha a sua cópia do jogo.");
    }

    void Dropped(object sender, DragEventArgs e)
    {
        if (runner != null || e.Data.GetData(DataFormats.FileDrop) is not string[] { Length: > 0 } paths) return;
        if (view != View.Install) ShowInstall();
        foreach (var p in paths)
        {
            if (p.EndsWith(".nup", StringComparison.OrdinalIgnoreCase) && File.Exists(p)) AddDump(p, L("Déposé", "Dropped", "Solto"));
            else if (Directory.Exists(Path.Combine(p, "romfs")) && Directory.Exists(Path.Combine(p, "exefs")))
                UseFolders(Path.Combine(p, "romfs"), Path.Combine(p, "exefs"));
        }
    }

    void BrowseNup(object sender, RoutedEventArgs e)
    {
        var d = new OpenFileDialog { Filter = L("Copie du jeu (*.nup)|*.nup", "Game file (*.nup)|*.nup", "Arquivo do jogo (*.nup)|*.nup"), Title = L("Choisir votre copie du jeu", "Choose your game file", "Escolha o arquivo do seu jogo") };
        if (d.ShowDialog(this) == true) AddDump(d.FileName, L("Choisi", "Chosen", "Escolhido"));
    }

    /// The other way in: an unpacked game, its romfs and exefs folders (or one folder holding both)
    void BrowseFolders(object sender, RoutedEventArgs e)
    {
        var both = PickFolder(L("Choisir le dossier qui contient romfs et exefs (ou le dossier romfs)", "Choose the folder with romfs and exefs (or the romfs folder)", "Escolha a pasta com romfs e exefs (ou a pasta romfs)"));
        if (both == null) return;
        if (Directory.Exists(Path.Combine(both, "romfs")) && Directory.Exists(Path.Combine(both, "exefs")))
        {
            UseFolders(Path.Combine(both, "romfs"), Path.Combine(both, "exefs"));
            return;
        }
        var ex = PickFolder(L("Choisir maintenant le dossier exefs", "Now choose the exefs folder", "Agora escolha a pasta exefs"));
        if (ex != null) UseFolders(both, ex);
    }

    void UseFolders(string r, string x)
    {
        if (!File.Exists(Path.Combine(x, "main")))
        {
            MessageBox.Show(this, L("Ce dossier exefs ne contient pas de fichier nommé main.", "That exefs folder has no file named main.", "Essa pasta exefs não tem um arquivo chamado main."), Texts.Name);
            return;
        }
        if (RomfsProblem(r) is { } why)
        {
            MessageBox.Show(this, why, Texts.Name);
            return;
        }
        romfs = r;
        exefs = x;
        chosenNup = null;
        foldersNote = L($"Dossiers utilisés : {r} et {x}", $"Using the folders {r} and {x}", $"Usando as pastas {r} e {x}");
        FillDumps();
        Refresh();
    }

    const string RomfsTable = "ResourceSizeTable.Product.111.Nin_Ounce_NVN2.rsizetable.zs";   // (the supported version's)

    /// (issue 26) Why a romfs folder is not the supported game's RomFS, or null. One taken from another PC port's folder (a few
    /// packed .bin files, no subfolders) was accepted, built for 40 minutes, and the game closed in its first second
    string? RomfsProblem(string r)
    {
        var res = Path.Combine(r, "System", "Resource");
        if (File.Exists(Path.Combine(res, RomfsTable))) return null;
        var other = Directory.Exists(res) ? Directory.GetFiles(res, "ResourceSizeTable*").Select(Path.GetFileName).FirstOrDefault() : null;
        if (other != null)
            return L($"Ce dossier romfs vient d'une autre version du jeu ({other}). ReLink a besoin de la version dont le romfs contient " +
                     $"System\\Resource\\{RomfsTable} : faites une copie (dump) du jeu avec sa mise à jour, et prenez le romfs et l'exefs de cette même copie.",
                     $"This romfs folder is from another version of the game ({other}). ReLink needs the version whose romfs has " +
                     $"System\\Resource\\{RomfsTable}: dump the game with its update applied, and use the romfs and exefs of that same dump.", $"Esta pasta romfs é de outra versão do jogo ({other}). O ReLink precisa da versão cuja romfs tem " +
                     $"System\\Resource\\{RomfsTable}: faça o dump do jogo com a atualização aplicada, e use a romfs e a exefs desse mesmo dump.");
        return L("Ce dossier romfs n'est pas le RomFS du jeu : il n'a pas de fichier System\\Resource\\ResourceSizeTable. Les dossiers pris " +
                 "dans un autre portage PC ou un autre outil gardent le jeu dans leur propre format et ne marchent pas ici. Utilisez votre " +
                 "propre copie du jeu : le fichier .nup, ou les dossiers romfs et exefs extraits de lui.",
                 "This romfs folder is not the game's own RomFS: it has no System\\Resource\\ResourceSizeTable file. Folders taken from " +
                 "another PC port or tool hold the game in their own format and do not work here. Use your own dump of the game: the " +
                 ".nup file, or the romfs and exefs folders dumped from it.", "Esta pasta romfs não é a RomFS do próprio jogo: ela não tem o arquivo System\\Resource\\ResourceSizeTable. Pastas tiradas de " +
                 "outro port para PC ou ferramenta guardam o jogo no formato delas e não funcionam aqui. Use o seu próprio dump do jogo: o " +
                 "arquivo .nup, ou as pastas romfs e exefs extraídas dele.");
    }

    static string? PickFolder(string title)
    {
        var d = new OpenFolderDialog { Title = title };
        return d.ShowDialog() == true ? d.FolderName : null;
    }

    void BrowseOut(object sender, RoutedEventArgs e)
    {
        outPath = PickFolder(L("Choisir où installer le jeu", "Choose where the game goes", "Escolha onde o jogo vai ficar")) ?? outPath;
        Refresh();
    }

    // ================= 2: the install at work =================

    async void StartInstall()
    {
        SaveInstallChoices();
        if (launcherIsOld)
        {
            MainButton.IsEnabled = false;
            MainButtonText.Text = L("Téléchargement…", "Downloading…", "Baixando…");
            try { await Updater.RunLatestSetup(this, Array.Empty<string>()); }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message + L(" Vous pouvez aussi télécharger ReLink-Setup.exe depuis la page des versions.",
                                                     " You can also download ReLink-Setup.exe from the releases page.", " Você também pode baixar o ReLink-Setup.exe na página de releases."), Texts.Name);
                RefreshMode();
            }
            return;
        }
        var tools = Tools.Find();
        if (tools == null)
        {
            ShowProblem(L("Les outils de ReLink ne sont pas dans son dossier.", "The build tools are not in ReLink's folder.", "As ferramentas de compilação não estão na pasta do ReLink."), tools: true);
            return;
        }
        var args = new List<string>();
        if (romfs != null && exefs != null) args.AddRange(new[] { "--romfs", romfs, "--exefs", exefs });
        else if (chosenNup != null) args.AddRange(new[] { "--nup", chosenNup });
        else return;
        args.AddRange(new[] { "--out", outPath, "--language", s.Language });
        Run(tools, args);
    }

    static (string Id, string Build, string Update)[] Stages => new[]
    {
        ("download", "", L("Recevoir la mise à jour", "Download the update", "Baixar a atualização")),
        ("read", L("Lire votre copie du jeu", "Read your game files", "Ler os arquivos do seu jogo"), L("Lire les fichiers du jeu gardés dans le dossier", "Read the game files kept in the folder", "Ler os arquivos do jogo guardados na pasta")),
        ("lift", L("Traduire le jeu en code PC", "Translate the game to PC code", "Traduzir o jogo para código de PC"), L("Traduire le jeu en code PC", "Translate the game to PC code", "Traduzir o jogo para código de PC")),
        ("compile", L("Compiler", "Compile", "Compilar"), L("Compiler ce qui a changé", "Compile what changed", "Compilar o que mudou")),
        ("finish", L("Assembler le jeu", "Put the game together", "Montar o jogo"), L("Mettre le nouveau jeu en place", "Swap in the new game", "Colocar o jogo novo no lugar")),
    };
    static readonly Dictionary<string, (double From, double Size)> Weight = new()
    {
        ["download"] = (0, 0.04), ["read"] = (0.04, 0.04), ["lift"] = (0.08, 0.14), ["compile"] = (0.22, 0.74), ["finish"] = (0.96, 0.04),
    };

    void MakeRows(bool update)
    {
        StageList.Children.Clear();
        rows.Clear();
        current = null;
        SetStepNotes(update);
        foreach (var stage in Stages.Where(x => update || x.Id != "download"))
        {
            var row = new StepRow(update ? stage.Update : stage.Build);
            rows[stage.Id] = row;
            StageList.Children.Add(row.Root);
        }
        if (update) rows["read"].Small(L("Vos fichiers du jeu sont gardés : le .nup n'est pas nécessaire", "Your game files are kept: the .nup isn't needed", "Os arquivos do seu jogo ficam guardados: o .nup não é necessário"));
    }

    /// A step begins: the one before it is done, on the page and on the staff
    void Stage(string id)
    {
        if (current != null)
        {
            if (rows.TryGetValue(current, out var was)) was.Done();
            staff[current]?.Done();
        }
        current = id;
        if (rows.TryGetValue(id, out var row)) row.Begin();
        staff[id]?.Active();
    }

    void Run(Tools tools, List<string> args, bool keepRows = false)
    {
        if (!keepRows)
        {
            updating = false;
            MakeRows(update: false);
            BuildTitle.Text = L("Installation en cours", "Installing the game", "Instalando o jogo");
            BuildSub.Text = outPath;
        }
        log.Clear();
        Log.Clear();
        finished = "";
        compiled = 0;
        filesDone = filesOf = 0;
        filesChanged = -1;
        filesCompiled = 0;
        compileEta = -1;
        thisCompile = 0;
        previousTotal = -1;
        Show(View.Build);
        CancelLink.Visibility = Visibility.Visible;
        SetProgress(updating ? Weight["read"].From : 0);
        elapsed.Restart();
        compileClock.Reset();
        clock.Start();
        Tick();
        if (Storage.WorkChosen && !args.Contains("--work")) args.AddRange(new[] { "--work", Storage.Work });   // (the build files where the player chose)
        // (NVIDIA DLSS: the player ticked it on the Display page, which accepts NVIDIA's DLSS SDK license; the SDK comes from NVIDIA
        // at each build, only what is missing, into the launcher's own folder, never into the game folder's files shared with others)
        if (s.NvidiaDlss && !args.Contains("--fetch-ngx-sdk")) args.AddRange(new[] { "--fetch-ngx-sdk", "--accept-nvidia-license" });
        runner = new Runner(tools, args) { NgxSdk = s.NvidiaDlss ? Path.Combine(Storage.Base, "ngx-sdk") : null };
        runner.Line += l => Dispatcher.BeginInvoke(() => Line(l));
        runner.Finished += failure => Dispatcher.BeginInvoke(() => Ended(failure));
        runner.Start();
        UpdateSide();   // (now that it runs: an update or a build again lets Home, Options and Mods be looked at)
    }

    static readonly Regex FilesLine = new(@"^(\d+) files, "), FileLine = new(@"^\s+\S+\s+\d+ MB\s+\d+s");

    void Line(string line)
    {
        if (line.StartsWith("@@stage "))
        {
            var id = line.Split(' ')[1];
            Stage(id);
            if (id == "compile") compileClock.Restart();
            if (Weight.TryGetValue(id, out var w)) SetProgress(w.From);
        }
        else if (line.StartsWith("@@progress "))
        {
            if (double.TryParse(line[11..], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var f) && current != null
                && Weight.TryGetValue(current, out var w))
            {
                if (current == "compile") compiled = f;
                SetProgress(w.From + w.Size * Math.Clamp(f, 0, 1));
            }
            return;
        }
        else if (line.StartsWith("@@plan ") || line.StartsWith("@@file ") || line.StartsWith("@@eta ") || line.StartsWith("@@times "))
        {
            Number(line);
            return;
        }
        else if (line.StartsWith("@@done ")) finished = line[7..];
        else if (FilesLine.Match(line) is { Success: true } m) filesOf = int.Parse(m.Groups[1].Value);
        else if (FileLine.IsMatch(line)) filesDone++;
        if (current == "compile" && filesChanged < 0 && filesOf > 0 && rows.TryGetValue("compile", out var c))
            c.Small(L($"{filesDone} fichiers sur {filesOf}", $"{filesDone} of {filesOf} files", $"{filesDone} de {filesOf} arquivos"));
        AddLog(line);
    }

    /// The compile timer's lines: build.py says which files this run really rebuilds and how long each took before (the
    /// countdown), and make_game.py gives this build's times beside the last one's
    void Number(string line)
    {
        var p = line.Split(' ');
        switch (p[0])
        {
            case "@@plan" when p.Length == 3 && int.TryParse(p[1], out var changed) && int.TryParse(p[2], out var total):
                filesChanged = changed;
                filesOf = total;
                filesCompiled = 0;
                ChangedFiles();
                break;
            case "@@file" when p.Length == 4 && (p[3] is "new" or "again"):
                filesCompiled++;
                ChangedFiles();
                break;
            case "@@eta" when p.Length == 2 && double.TryParse(p[1], System.Globalization.NumberStyles.Float,
                                                              System.Globalization.CultureInfo.InvariantCulture, out var eta):
                compileEta = eta;
                break;
            case "@@times" when p.Length == 4:
                long.TryParse(p[2], out thisCompile);
                long.TryParse(p[3], out previousTotal);
                break;
        }
    }

    void ChangedFiles()
    {
        if (filesChanged < 0 || !rows.TryGetValue("compile", out var row)) return;
        row.Small(filesChanged == 0 ? L("Tout est déjà compilé", "Everything is already built", "Tudo já está compilado")
                : filesChanged >= filesOf ? L($"{filesCompiled} fichiers sur {filesChanged}", $"{filesCompiled} of {filesChanged} files", $"{filesCompiled} de {filesChanged} arquivos")
                : L($"{filesCompiled} fichiers changés sur {filesChanged}", $"{filesCompiled} of {filesChanged} changed files", $"{filesCompiled} de {filesChanged} arquivos alterados"));
    }

    void AddLog(string line)
    {
        log.AppendLine(line);
        if (log.Length > 600_000) log.Remove(0, log.Length - 400_000);
        if (Log.Visibility == Visibility.Visible)
        {
            Log.AppendText(line + "\n");
            Log.ScrollToEnd();
        }
    }

    void SetProgress(double f) => PercentText.Text = $"{(int)(Math.Clamp(f, 0, 1) * 100)} %";

    void Tick()
    {
        foreach (var r in rows.Values) r.Tick();
        if (runner == null) return;
        var tail = updating ? L(" Le jeu actuel reste jouable jusqu'à la fin.", " Your current game stays playable until the end.", " O seu jogo atual continua jogável até o fim.")
                            : L(" Le PC reste utilisable.", " Your PC stays usable.", " O seu PC continua utilizável.");
        if (current == "compile")
        {
            var left = compileEta >= 0 ? TimeLeft(compileEta)
                : compiled > 0.03 ? TimeLeft(compileClock.Elapsed.TotalSeconds / compiled * (1 - compiled) + 60)
                : L("Estimation en cours.", "Estimating.", "Estimando.");
            BuildNote.Text = left + tail;
            return;
        }
        BuildNote.Text = (updating ? L("Mise à jour en cours.", "Updating.", "Atualizando.") : L("Compter 10 à 45 minutes en tout.", "Allow 10 to 45 minutes in all.", "Reserve de 10 a 45 minutos no total.")) + tail;
    }

    static string TimeLeft(double seconds)
    {
        if (seconds < 45) return L("Moins d'une minute.", "Less than a minute left.", "Falta menos de um minuto.");
        if (seconds < 90) return L("Encore une minute environ.", "About a minute left.", "Falta cerca de um minuto.");
        var minutes = (int)Math.Round(seconds / 60);
        return minutes >= 60 ? L($"Encore {minutes / 60} h {minutes % 60} min environ.", $"About {minutes / 60} h {minutes % 60} min left.", $"Faltam cerca de {minutes / 60} h {minutes % 60} min.")
                             : L($"Encore {minutes} minutes environ.", $"About {minutes} minutes left.", $"Faltam cerca de {minutes} minutos.");
    }

    void Ended(string? failure)
    {
        clock.Stop();
        elapsed.Stop();
        runner = null;
        if (finished.Length > 0)
        {
            foreach (var r in rows.Values) r.Done();
            foreach (var n in staff.Notes) n.Done();
            SetProgress(1);
            Built();
            return;
        }
        if (failure == "cancelled")
        {
            if (updating && Game.Installed(outPath)) { ShowHome(); CheckLatest(); }
            else ShowInstall();
            return;
        }
        if (current != null)
        {
            if (rows.TryGetValue(current, out var row)) row.Stopped();
            staff[current]?.Problem();
        }
        ShowProblem(failure ?? L("Les outils se sont arrêtés sans dire pourquoi.", "The build tools stopped without saying why.", "As ferramentas de compilação pararam sem dizer por quê."),
                    tools: failure?.Contains("antivirus", StringComparison.OrdinalIgnoreCase) == true);
    }

    void LogToggled(object sender, RoutedEventArgs e)
    {
        var show = Log.Visibility != Visibility.Visible;
        Log.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        StageList.Visibility = show ? Visibility.Collapsed : Visibility.Visible;
        LogLink.Content = show ? L("Masquer les détails", "Hide details", "Esconder detalhes") : L("Afficher les détails", "Show details", "Mostrar detalhes");
        if (show)
        {
            Log.Text = log.ToString();
            Log.ScrollToEnd();
        }
    }

    void CancelBuild(object sender, RoutedEventArgs e)
    {
        download?.Cancel();
        runner?.Cancel();
    }

    // ================= 3: the game is ready: Home, with the first start's line in the sky =================
    // (the screen that said "ready" said again what Home says; its switches and the space ReLink keeps are in Options, Game)

    CheckBox DesktopTick = null!, StartTick = null!;
    TextBlock StorageNote = null!;
    Button FreeLink = null!;
    Border StorageRow = null!;
    bool justBuilt;   // (the sky says what the first start does, until it is made)

    /// Options, Game: the shortcuts as they are, and what ReLink keeps on the PC (once a build has measured it)
    void BuildShortcutRows()
    {
        SwitchRow(RowsGame, L("Raccourci sur le bureau", "Desktop shortcut", "Atalho na área de trabalho"), null, out DesktopTick, out _);
        SwitchRow(RowsGame, L("Entrée dans le menu Démarrer", "Start menu entry", "Entrada no menu Iniciar"), null, out StartTick, out _);
        DesktopTick.IsChecked = File.Exists(Finder.DesktopLink);
        StartTick.IsChecked = File.Exists(Finder.StartLink);
        DesktopTick.Click += ShortcutsChanged;
        StartTick.Click += ShortcutsChanged;
        StorageNote = new TextBlock { Style = St("RowHint"), Margin = new Thickness(0) };
        FreeLink = new Button { Style = St("Link"), HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 8, 0, 0) };
        FreeLink.Click += FreeSpace;
        StorageRow = new Border { Style = St("Row"), Child = new StackPanel { Children = { StorageNote, FreeLink } }, Visibility = Visibility.Collapsed };
        RowsGame.Children.Add(StorageRow);
    }

    /// The build done: the shortcuts (made at the first install, made again at an update), the folder the launcher's, Home
    void Built()
    {
        if (!updating)
        {
            Finder.Shortcuts(finished, true, true);
            DesktopTick.IsChecked = StartTick.IsChecked = true;
        }
        else Finder.RefreshShortcuts(finished);
        if (Finder.ShortcutError != null) AddLog("shortcut not made: " + Finder.ShortcutError);
        // the game folder is the launcher's from now on: its settings page reads and writes it
        s.Folder = finished;
        try { s.LoadGame(); s.Save(); } catch (Exception) { }
        _ = FreeAfterBuild();
        justBuilt = true;
        NavHome.IsChecked = true;
        ShowHome();
        CheckLatest();
    }

    void ShortcutsChanged(object sender, RoutedEventArgs e)
    {
        if (s.Folder.Length > 0) Finder.Shortcuts(s.Folder, DesktopTick.IsChecked == true, StartTick.IsChecked == true);
    }

    /// What ReLink keeps on the PC, said once the game is ready, with the way to let it go
    async Task ShowStorageUse()
    {
        StorageRow.Visibility = Visibility.Collapsed;
        var (total, freeable) = await Task.Run(Storage.Measure);
        if (total < 100L << 20) return;
        var where = Storage.WorkChosen ? L($"{Storage.Base} et {Storage.Work}", $"{Storage.Base} and {Storage.Work}", $"{Storage.Base} e {Storage.Work}") : Storage.Base;
        StorageNote.Text = freeable >= 100L << 20
            ? L($"ReLink garde {Bytes(total)} sur ce PC ({where}). {Bytes(freeable)} ne servent qu'à rendre la prochaine mise à jour plus rapide et peuvent être libérés : le jeu reste jouable.",
                $"ReLink keeps {Bytes(total)} on this PC ({where}). {Bytes(freeable)} of it only makes the next update quicker and can be freed: your game keeps playing.", $"O ReLink ocupa {Bytes(total)} neste PC ({where}). {Bytes(freeable)} disso só deixa a próxima atualização mais rápida e pode ser liberado: o seu jogo continua funcionando.")
            : L($"ReLink garde {Bytes(total)} sur ce PC ({where}) : le lanceur et ses outils.", $"ReLink keeps {Bytes(total)} on this PC ({where}): the launcher and its tools.", $"O ReLink ocupa {Bytes(total)} neste PC ({where}): o launcher e as ferramentas dele.");
        FreeLink.Visibility = freeable >= 100L << 20 ? Visibility.Visible : Visibility.Collapsed;
        FreeLink.IsEnabled = true;
        FreeLink.Content = L("Libérer cet espace", "Free up this space", "Liberar este espaço");
        StorageRow.Visibility = Visibility.Visible;
    }

    /// The build files (translated code, compiled objects), the downloads and older launcher versions go as soon as the game is
    /// ready; the next update then translates and compiles again. RELINK_KEEP_BUILD_FILES=1 keeps them for quick updates.
    async Task FreeAfterBuild()
    {
        if (!Storage.KeepBuildFiles)
        {
            var left = await Task.Run(Storage.Free);
            if (left >= 100L << 20) AddLog($"build files: {Bytes(left)} could not be deleted (a file in use); they go after the next build");
        }
        await ShowStorageUse();
    }

    async void FreeSpace(object sender, RoutedEventArgs e)
    {
        FreeLink.IsEnabled = false;
        FreeLink.Content = L("Libération…", "Freeing…", "Liberando…");
        var left = await Task.Run(Storage.Free);
        await ShowStorageUse();
        if (left >= 100L << 20)
            StorageNote.Text += L($" ({Bytes(left)} n'ont pas pu être supprimés : un fichier est utilisé. Fermez le jeu et réessayez.)",
                                  $" ({Bytes(left)} could not be deleted: a file is in use. Close the game and try again.)", $" ({Bytes(left)} não puderam ser apagados: um arquivo está em uso. Feche o jogo e tente de novo.)");
    }

    // ================= when something goes wrong =================

    void ShowProblem(string why, bool tools)
    {
        lastFailure = why;
        ProblemSteps.Children.Clear();
        ProblemSub.Text = updating ? L("Votre jeu actuel n'a pas changé et reste jouable.", "Your current game is unchanged and still plays.", "O seu jogo atual não mudou e continua funcionando.")
                                   : L("Rien n'a été modifié dans le dossier du jeu.", "Nothing was changed in your game folder.", "Nada foi mudado na pasta do seu jogo.");
        if (tools)
        {
            ProblemTitle.Text = L("Les outils sont à réparer", "The build tools need repairing", "As ferramentas de compilação precisam de reparo");
            ProblemHead.Text = L("Votre antivirus a retiré une partie des outils", "Your antivirus removed part of the build tools", "O seu antivírus removeu parte das ferramentas de compilação");
            ProblemText.Text = why + "\n\n" + L("Les antivirus prennent parfois par erreur des outils de développement non signés pour une menace.",
                                                "Antivirus programs sometimes flag unsigned developer tools by mistake.", "Os antivírus às vezes marcam por engano ferramentas de desenvolvimento sem assinatura.");
            var steps = new[]
            {
                L("Ouvrez Sécurité Windows, puis Historique de protection, et restaurez les fichiers retirés.", "Open Windows Security, then Protection history, and restore the removed files.", "Abra a Segurança do Windows, depois o Histórico de proteção, e restaure os arquivos removidos."),
                L($"Ou ajoutez {Storage.Base} aux exclusions.", $"Or add {Storage.Base} to its exclusions.", $"Ou adicione {Storage.Base} às exclusões dele."),
                L("Puis choisissez Réparer : ReLink télécharge de nouveau ses outils et vérifie chacun.", "Then press Repair: ReLink downloads its tools again and checks each one.", "Depois aperte Reparar: o ReLink baixa as ferramentas de novo e verifica cada uma."),
            };
            for (var i = 0; i < steps.Length; i++)
                ProblemSteps.Children.Add(new TextBlock { Text = $"{i + 1}.  {steps[i]}", Style = St("Body"), Margin = new Thickness(0, 6, 0, 0) });
        }
        else
        {
            ProblemTitle.Text = updating ? L("La mise à jour s'est arrêtée", "The update stopped", "A atualização parou") : L("L'installation s'est arrêtée", "The install stopped", "A instalação parou");
            ProblemHead.Text = L("Ce qui s'est passé", "What went wrong", "O que deu errado");
            ProblemText.Text = why;
        }
        repairable = tools;   // (Repair is the card's button: Sidebar.cs)
        RepairLink.Visibility = Visibility.Collapsed;
        DetailsLink.Content = L("Afficher les détails", "Show details", "Mostrar detalhes");
        CopyLink.Content = L("Copier pour un rapport", "Copy for a report", "Copiar para um relatório");
        Show(View.Problem);
    }

    void ProblemShowDetails(object sender, RoutedEventArgs e)
    {
        var lines = log.ToString().Split('\n');
        ProblemSteps.Children.Clear();
        ProblemSteps.Children.Add(new TextBox
        {
            Text = string.Join("\n", lines.Skip(Math.Max(0, lines.Length - 80))), IsReadOnly = true, FontFamily = (System.Windows.Media.FontFamily)FindResource("Mono"),
            FontSize = 11.5, Foreground = Res("BoneSoft"), Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(0x66, 0, 0, 0)),
            BorderThickness = new Thickness(0), Padding = new Thickness(8), TextWrapping = TextWrapping.Wrap,
        });
    }

    void CopyForReport(object sender, RoutedEventArgs e)
    {
        var lines = log.ToString().Split('\n');
        var text = $"Project ReLink {Tools.Find()?.Version ?? "(source)"} on Windows {Environment.OSVersion.Version}\n" +
                   $"{ProblemTitle.Text}: {lastFailure}\n\n" + string.Join("\n", lines.Skip(Math.Max(0, lines.Length - 200)));
        try { Clipboard.SetText(text); CopyLink.Content = L("Copié", "Copied", "Copiado"); } catch (Exception) { }
    }

    void Back(object sender, RoutedEventArgs e)
    {
        if (updating && Game.Installed(outPath)) { ShowHome(); CheckLatest(); }
        else ShowInstall();
    }

    /// The setup (ReLink-Setup.exe beside this app) checks every part against its checksum and downloads the missing ones
    void Repair(object sender, RoutedEventArgs e)
    {
        var setup = Path.Combine(AppContext.BaseDirectory, "ReLink-Setup.exe");
        if (File.Exists(setup))
        {
            Process.Start(new ProcessStartInfo(setup, "--repair") { UseShellExecute = false, WorkingDirectory = AppContext.BaseDirectory });
            Application.Current.Shutdown();
            return;
        }
        Open(ReleasesPage);
    }

    // ================= the update (Updater.cs) =================

    CancellationTokenSource? download;

    public void BeginUpdate(string folder, string? tag)
    {
        updating = true;
        outPath = folder;
        finished = "";
        var to = tag ?? L("la dernière version", "the latest version", "a versão mais recente");
        var from = Game.Version(folder) ?? Tools.Find()?.Version;
        MakeRows(update: true);
        BuildTitle.Text = L("Mise à jour du jeu", "Updating your game", "Atualizando o seu jogo");
        BuildSub.Text = from != null && from != to ? L($"De {from} à {to}", $"From {from} to {to}", $"De {from} para {to}") : to;
        BuildNote.Text = L("Réception de la mise à jour.", "Getting the update.", "Baixando a atualização.");
        Show(View.Build);
        SetProgress(0);
        Stage("download");
        CancelLink.Visibility = Visibility.Visible;
    }

    public CancellationToken DownloadToken => (download = new CancellationTokenSource()).Token;

    public void DownloadProgress(double f)
    {
        rows.GetValueOrDefault("download")?.Small($"{(int)(f * 100)} %");
        SetProgress(Weight["download"].Size * f);
    }

    public void Note(string line)
    {
        AddLog(line);
        rows.GetValueOrDefault("download")?.Small(line);
    }

    public void DownloadFailed(string why)
    {
        download = null;
        rows.GetValueOrDefault("download")?.Stopped();
        staff["download"]?.Problem();
        ShowProblem(why, tools: false);
        ProblemTitle.Text = L("La mise à jour n'a pas pu être téléchargée", "The update could not be downloaded", "Não foi possível baixar a atualização");
    }

    /// The game folder built again with the tools there are (NVIDIA DLSS turned on: what it changes is compiled, NVIDIA's SDK
    /// fetched first; or the exefs mods changed: Home.cs RebuildClick), on the update's screen with nothing to download
    internal void Rebuild(Tools tools, string folder, string? title = null)
    {
        updating = true;
        outPath = folder;
        finished = "";
        MakeRows(update: true);
        rows.GetValueOrDefault("download")?.Small(L("Rien à télécharger", "Nothing to download", "Nada para baixar"));
        BuildTitle.Text = title ?? L("Reconstruction du jeu avec NVIDIA DLSS", "Building your game again with NVIDIA DLSS", "Compilando o jogo de novo com o NVIDIA DLSS");
        BuildSub.Text = folder;
        Run(tools, new List<string> { "--update", folder }, keepRows: true);
    }

    /// The new tools are here: the folder built again with them (only what changed is compiled)
    internal void UpdateBuild(Tools tools, string folder)
    {
        download = null;
        rows.GetValueOrDefault("download")?.Small(L("Outils vérifiés", "Build tools checked", "Ferramentas de compilação verificadas"));
        Run(tools, new List<string> { "--update", folder }, keepRows: true);
    }
}
