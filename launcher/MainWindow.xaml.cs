using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using static ReLink.Texts;

namespace ReLink;

/// One window for everything a player does: install the game from their own copy, update it, set it up and start it.
/// On the left, the name, the staff and what to do next; on the right, the tablet, which shows one page at a time (Install.cs:
/// the install and the update; Home.cs: the settings and the game itself).
public partial class MainWindow : Window
{
    enum View { Install, Build, Problem, Home }

    Settings s = new();
    readonly Staff staff;
    readonly DispatcherTimer skyTimer = new() { Interval = TimeSpan.FromMinutes(5) };
    View view = View.Install;
    bool snapshot;

    static Brush Res(string key) => (Brush)Application.Current.FindResource(key);
    static Style St(string key) => (Style)Application.Current.FindResource(key);

    public MainWindow()
    {
        InitializeComponent();
        // (a snapshot is for tests and pictures: its window is made off the screen, out of the taskbar and without taking the
        // focus. On the screen it flashed open and shut at every picture, which looked like the launcher starting and dying)
        if (Arg("--snapshot") != null)
        {
            WindowStartupLocation = WindowStartupLocation.Manual;
            Left = -32000;
            Top = -32000;
            ShowInTaskbar = false;
            ShowActivated = false;
        }
        staff = new Staff(StaffCanvas);
        Scene.Draw(Sky, Sky.Width, Sky.Height, Scene.Hour());
        skyTimer.Tick += (_, _) => Scene.Draw(Sky, Sky.Width, Sky.Height, Scene.Hour());
        skyTimer.Start();
        clock.Tick += (_, _) => Tick();
        padTimer.Tick += (_, _) => UpdatePads();
        ApplyTexts();
        WatchNote();
        BuildHomeRows();
        HomeView.PreviewKeyDown += OptionsKey;   // (the options as the in-game menu: the arrows, Q and E)
        // (the window has no title bar: it moves by any place that is not a control)
        MouseLeftButtonDown += (_, e) =>
        {
            if (e.ButtonState != MouseButtonState.Pressed) return;
            try { DragMove(); } catch (InvalidOperationException) { }
        };
        Drop += Dropped;
        Loaded += (_, _) => Start();
        Closing += (_, _) => Native.RestoreRefresh();
        Activated += (_, _) => { if (view == View.Home && game == null && !snapshot) ShowMods(); };   // (mods added while it was open)
        AppDomain.CurrentDomain.UnhandledException += (_, _) => Native.RestoreRefresh();
    }

    static string? Arg(string name)
    {
        var a = Environment.GetCommandLineArgs();
        var i = Array.IndexOf(a, name);
        return i >= 0 && i + 1 < a.Length ? a[i + 1] : null;
    }

    void ApplyTexts()
    {
        HeroSub.Text = L("recompilé pour votre PC", "recompiled for your PC", "recompilado para o seu PC");
        SidebarTexts();
        LooksHead.Text = L("L'image", "How it looks", "Visual");
        MovesHead.Text = L("La fluidité", "How it moves", "Movimento");
        LooksMore.Text = MovesMore.Text = L("Changer ›", "Change ›", "Mudar ›");
        ModsHead.Text = "Mods";
        ModsMore.Text = L("Gérer ›", "Manage ›", "Gerenciar ›");
        NewsHead.Text = L("Nouveautés", "What's new", "Novidades");
        NewsMore.Text = L("Lire les notes ›", "Read the notes ›", "Ler as notas ›");
        HeroPlayText.Text = L("Jouer", "Play", "Jogar");
        ModsTitle.Text = "Mods";
        ModsIntro.Text = L("Les mods du dossier mods du jeu. Les mods romfs (textures, modèles, sons, textes) comptent dès le prochain lancement ; les mods exefs (patchs de code) demandent de reconstruire le jeu.",
                           "The mods in the game folder's mods. RomFS mods (textures, models, sounds, text) count from the next start; exefs mods (code patches) need the game built again.", "Os mods na pasta mods do jogo. Mods de RomFS (texturas, modelos, sons, texto) valem a partir da próxima abertura; mods de exefs (patches de código) precisam que o jogo seja compilado de novo.");
        ModsOpenFolder.Content = L("Ouvrir le dossier mods", "Open the mods folder", "Abrir a pasta de mods");
        ModsRebuild.Content = L("Reconstruire avec les patchs de code", "Build again with the code patches", "Compilar de novo com os patches de código");

        InstallTitle.Text = L("Installer le jeu", "Install the game", "Instalar o jogo");
        InstallSub.Text = L("ReLink fabrique le jeu sur ce PC à partir de votre propre copie. Il faut le jeu avec sa mise à jour v131072 (build e0f3ccfa) : les autres versions ne fonctionnent pas. Cela se fait une fois et prend de 10 à 45 minutes.",
                            "ReLink builds the game on this PC from your own copy. It needs the game with its update v131072 (build e0f3ccfa): other versions do not work. It happens once and takes 10 to 45 minutes.", "O ReLink monta o jogo neste PC a partir da sua própria cópia. Ele precisa do jogo com a atualização v131072 (build e0f3ccfa): outras versões não funcionam. Acontece uma vez só e leva de 10 a 45 minutos.");
        CopyHead.Text = L("Votre copie du jeu", "Your copy of the game", "A sua cópia do jogo");
        BrowseNupLink.Content = L("Choisir un fichier .nup…", "Choose a .nup file…", "Escolher um arquivo .nup…");
        BrowseFoldersLink.Content = L("Choisir les dossiers romfs et exefs…", "Choose the romfs and exefs folders…", "Escolher as pastas romfs e exefs…");
        OutHead.Text = L("Dossier d'installation", "Install folder", "Pasta de instalação");
        OutChange.Content = L("Changer…", "Change…", "Mudar…");

        LogLink.Content = L("Afficher les détails", "Show details", "Mostrar detalhes");
        CancelLink.Content = L("Annuler", "Cancel", "Cancelar");
        BackLink.Content = L("Retour", "Back", "Voltar");
        RepairLink.Content = L("Réparer", "Repair", "Reparar");
        DetailsLink.Content = L("Afficher les détails", "Show details", "Mostrar detalhes");
        CopyLink.Content = L("Copier pour un rapport", "Copy for a report", "Copiar para um relatório");

        TabDisplay.Content = L("Affichage", "Display", "Tela");
        TabSmooth.Content = L("Fluidité", "Smoothness", "Fluidez");
        TabGame.Content = L("Jeu", "Game", "Jogo");
        TabControls.Content = L("Contrôles", "Controls", "Controles");
        HomeFoot.Text = HeroFoot.Text = L("Le menu en jeu (Inser ou F1) change aussi ces réglages.", "The in-game menu (Insert or F1) changes these settings too.", "O menu do jogo (Insert ou F1) também muda estas configurações.");

        PlayingTitle.Text = L("Bonne partie", "Enjoy the game", "Bom jogo");
        RestoreNow.Content = L("Remettre la fréquence de l'écran maintenant", "Put the display's refresh rate back now", "Voltar a taxa de atualização da tela agora");
    }

    // ================= the start =================

    async void Start()
    {
        var a = Environment.GetCommandLineArgs();
        snapshot = Arg("--snapshot") != null;
        s = Settings.Load(Arg("--launch")?.TrimEnd('\\', '/'));
        LoadInstallChoices();
        ShowSettings();
        UpdateScreen();
        UpdatePads();
        padTimer.Start();
        if (snapshot)
        {
            Preview(Arg("--view") ?? "home");
            TakeSnapshot(Arg("--snapshot")!);
            return;
        }
        if (Arg("--check") is { } report)   // (tests: what the launcher sees, without touching the display)
        {
            var (w, h, hz) = Native.CurrentMode();
            var ok = Native.SetRefresh(60, true, out var why);
            File.WriteAllLines(report, new[]
            {
                $"folder={s.Folder}", $"game={(s.Folder == "" ? "" : Game.Exe(s.Folder))}", $"version={(s.Folder == "" ? "" : Game.Version(s.Folder))}",
                $"tools={Tools.Find()?.Root}", $"mode={w}x{h}@{hz}", $"rates={string.Join(",", Native.RatesAt(w, h))}",
                $"set60_test={(ok ? "ok" : "no")}: {why}", $"pads={Native.PadsConnected()}", $"language={s.Language}",
            });
            Close();
            return;
        }
        if (a.Contains("--update"))
        {
            Updater.FromCommandLine(this);
            return;
        }
        if (s.Folder != "" && !a.Contains("--install"))
        {
            ShowHome();
            staff.Play();
            CheckLatest();
            if (a.Contains("--play")) Play();   // (a shortcut that starts the game at once: ReLink.exe --launch <folder> --play)
            return;
        }
        ShowInstall();
        staff.Play();
        var found = await Task.Run(Finder.FindDumps);
        foreach (var d in found)
            if (!dumps.Any(x => string.Equals(x.Path, d.Path, StringComparison.OrdinalIgnoreCase))) dumps.Add(d);
        FillDumps();
        CheckAll();
        latestTag = await Updater.LatestTag();
        RefreshMode();
    }

    // ================= the pages =================

    void Show(View v)
    {
        view = v;
        var pages = new (View Is, FrameworkElement Page)[]
        {
            (View.Install, InstallView), (View.Build, BuildView), (View.Problem, ProblemView),
        };
        foreach (var (k, page) in pages)
        {
            page.Visibility = k == v ? Visibility.Visible : Visibility.Collapsed;
            if (k == v) Appear(page);
        }
        // (Home shows one of its pages: Home itself when coming back from the install, the build or a problem)
        if (v == View.Home && NavHome.IsChecked != true && NavOptions.IsChecked != true && NavMods.IsChecked != true) NavHome.IsChecked = true;
        ShowHomePage();
        ProgressBlock.Visibility = v == View.Build ? Visibility.Visible : Visibility.Collapsed;
        UpdateSide();   // (Sidebar.cs: the side for this page)
    }

    void Appear(FrameworkElement page)
    {
        if (snapshot) return;
        page.Opacity = 0;
        page.BeginAnimation(OpacityProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(220)));
    }

    /// Home's two pages, as the side's choice says: the sky and what Play will do, or the options
    void ShowHomePage()
    {
        var home = view == View.Home;
        var hero = home && NavHome.IsChecked == true;
        var options = home && NavOptions.IsChecked == true;
        var mods = home && NavMods.IsChecked == true;
        if (hero && HeroView.Visibility != Visibility.Visible) Appear(HeroView);
        if (options && HomeView.Visibility != Visibility.Visible) Appear(HomeView);
        if (mods && ModsView.Visibility != Visibility.Visible) { ShowModsPage(); Appear(ModsView); }
        HeroView.Visibility = hero ? Visibility.Visible : Visibility.Collapsed;
        HomeView.Visibility = options ? Visibility.Visible : Visibility.Collapsed;
        ModsView.Visibility = mods ? Visibility.Visible : Visibility.Collapsed;
        // (Home's Play is in the sky: the card's goes there; on the options and the mods it stays at hand)
        if (home) UpdateSide();
    }

    void NavChanged(object sender, RoutedEventArgs e)
    {
        if (HeroView == null) return;   // (the first one is checked while the window is still being made)
        if (view == View.Build && CanBrowse && sender is RadioButton { IsChecked: true }) { Show(View.Home); return; }   // (looking around while it builds)
        if (BackToHome && sender is RadioButton { IsChecked: true }) { ShowHome(); CheckLatest(); return; }   // (out of "Install again")
        ShowHomePage();
        if (NavOptions.IsChecked == true) Dispatcher.BeginInvoke(FocusSelectedRow, DispatcherPriority.Input);
    }

    /// The cards of Home: each opens the options at its page
    /// (on the press, and kept from the window: a press anywhere else that is not a control moves the window, DragMove, which
    /// takes the mouse and the release with it)
    void LooksClick(object sender, MouseButtonEventArgs e) { e.Handled = true; TabDisplay.IsChecked = true; NavOptions.IsChecked = true; }
    void MovesClick(object sender, MouseButtonEventArgs e) { e.Handled = true; TabSmooth.IsChecked = true; NavOptions.IsChecked = true; }

    void MainClick(object sender, RoutedEventArgs e)
    {
        if (view == View.Install) StartInstall();
        else if (view == View.Problem && repairable) Repair(sender, e);
        else if (Browsing) Show(View.Build);   // (Home looked at while the game builds: back to the progress)
        else if (view == View.Home) Play();
    }

    void TabChanged(object sender, RoutedEventArgs e)
    {
        if (PageDisplay == null) return;   // (the first tab is checked while the window is still being made)
        foreach (var (tab, page) in new[] { (TabDisplay, PageDisplay), (TabSmooth, PageSmooth), (TabGame, PageGame), (TabControls, PageControls) })
            page.Visibility = tab.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        ShowRowHelp();
        if (HomeView.IsVisible) Dispatcher.BeginInvoke(FocusSelectedRow, DispatcherPriority.Input);
    }

    // ================= links =================

    static void Open(string what)
    {
        try { Process.Start(new ProcessStartInfo(what) { UseShellExecute = true }); } catch (Exception) { }
    }

    void OpenHelp(object sender, RoutedEventArgs e) => Open(HelpPage);

    void OpenDiscord(object sender, RoutedEventArgs e) => Open(DiscordInvite);

    void OpenGameFolder(object sender, RoutedEventArgs e)
    {
        var folder = s.Folder;
        if (folder != "" && Directory.Exists(folder)) Open(folder);
    }

    /// (Game tab, Shaders) the prepared shaders deleted, after a confirmation: the next start prepares them all again
    void PrepareShadersAgain(object sender, RoutedEventArgs e)
    {
        var folder = s.Folder;
        if (folder == "" || !Game.Installed(folder)) return;
        if (game != null || Process.GetProcessesByName("relink-game").Length > 0)
        {
            MessageBox.Show(this, L("Fermez le jeu d'abord.", "Close the game first.", "Feche o jogo primeiro."), Texts.Name);
            return;
        }
        var (files, bytes) = Game.PreparedShaders(folder);
        if (files == 0)
        {
            MessageBox.Show(this, L("Il n'y a pas de shaders préparés à supprimer : le prochain lancement les préparera.",
                                    "There are no prepared shaders to delete: the next start will prepare them.", "Não há shaders preparados para apagar: a próxima abertura vai prepará-los."), Texts.Name);
            return;
        }
        var size = bytes >= 1L << 30 ? $"{bytes / (double)(1L << 30):0.0} GB" : $"{bytes >> 20} MB";
        if (MessageBox.Show(this,
                L($"Supprimer les shaders préparés ({size}) ? Le prochain lancement les préparera de nouveau, ce qui prend 10 à 15 minutes. Vos sauvegardes ne sont pas touchées.",
                  $"Delete the prepared shaders ({size})? The next start will prepare them again, which takes 10 to 15 minutes. Your saves are not touched.", $"Apagar os shaders preparados ({size})? A próxima abertura vai prepará-los de novo, o que leva de 10 a 15 minutos. Os seus saves não são mexidos."),
                Texts.Name, MessageBoxButton.YesNo) != MessageBoxResult.Yes)
            return;
        var why = Game.DeletePreparedShaders(folder);
        MainNote.Text = why == null
            ? L("Shaders supprimés : ils seront préparés au prochain lancement.", "Shaders deleted: they will be prepared at the next start.", "Shaders apagados: eles serão preparados na próxima abertura.")
            : L("Shaders pas tous supprimés : ", "Not all the shaders could be deleted: ", "Nem todos os shaders puderam ser apagados: ") + why;
    }

    // ================= the window =================

    void Minimize(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    void CloseWindow(object sender, RoutedEventArgs e)
    {
        if (runner != null && MessageBox.Show(this, L("Arrêter l'installation et fermer ?", "Stop the install and close?", "Parar a instalação e fechar?"), Texts.Name, MessageBoxButton.YesNo) != MessageBoxResult.Yes) return;
        runner?.Cancel();
        Close();
    }

    // ================= pictures of the window, for checking it (nothing is run) =================
    //   ReLink.exe --snapshot out.png --view install|build|update|ready|problem|home|home-update|home-mods|home-building|mods|options|smooth|game|controls|playing

    void Preview(string what)
    {
        switch (what)
        {
            case "install":
                dumps.Clear();
                dumps.Add(new Dump(@"C:\Users\player\Downloads\ocarina-of-time.nup", L("Téléchargements", "Downloads", "Downloads"), 31L << 30, DateTime.Now.AddDays(-3)));
                dumps.Add(new Dump(@"D:\dumps\ocarina-v0.nup", "D:", 29L << 30, DateTime.Now.AddDays(-40)));
                checks[dumps[0].Path] = (true, "This is the supported version.");
                checks[dumps[1].Path] = (false, "This is build 1a2b3c4d. ReLink supports build e0f3ccfa.");
                chosenNup = dumps[0].Path;
                outPath = @"E:\Games\ReLink";
                ShowInstall();
                break;
            case "build":
                outPath = @"E:\Games\ReLink";
                MakeRows(update: false);
                Show(View.Build);
                BuildTitle.Text = L("Installation en cours", "Installing the game", "Instalando o jogo");
                BuildSub.Text = outPath;
                Stage("read"); rows["read"].Small(L("Version vérifiée", "Version checked", "Versão verificada"));
                Stage("lift");
                Stage("compile"); rows["compile"].Small(L("31 fichiers sur 58", "31 of 58 files", "31 de 58 arquivos"));
                SetProgress(0.54);
                BuildNote.Text = L("Encore 19 minutes environ. Le PC reste utilisable.", "About 19 minutes left. Your PC stays usable.", "Faltam cerca de 19 minutos. O seu PC continua utilizável.");
                break;
            case "update":
                BeginUpdate(outPath, "v0.7.0");
                Stage("read"); rows["download"].Small(L("3 Mo, outils inchangés", "3 MB, build tools unchanged", "3 MB, ferramentas de compilação sem mudança"));
                Stage("lift");
                Stage("compile"); rows["compile"].Small(L("7 fichiers changés sur 516", "7 of 516 files changed", "7 de 516 arquivos alterados"));
                SetProgress(0.62);
                BuildNote.Text = L("Encore 3 minutes environ. Le jeu actuel reste jouable jusqu'à la fin.", "About 3 minutes left. Your current game stays playable until the end.", "Faltam cerca de 3 minutos. O seu jogo atual continua jogável até o fim.");
                break;
            case "ready":
                MakeRows(update: false);
                foreach (var id in new[] { "read", "lift", "compile", "finish" }) Stage(id);
                finished = outPath;
                elapsed.Restart();
                justBuilt = true;
                ShowHome();
                ShowVersion("v0.7.0", "v0.7.0", true, true);
                break;
            case "problem":
                MakeRows(update: false);
                Stage("read"); Stage("lift"); Stage("compile");
                ShowProblem(L("Des fichiers du compilateur de ReLink ont disparu (clang.exe dans tools\\llvm-mingw\\bin).",
                              "Files of ReLink's own compiler are missing (clang.exe in tools\\llvm-mingw\\bin).", "Faltam arquivos do compilador do próprio ReLink (clang.exe em tools\\llvm-mingw\\bin)."), tools: true);
                break;
            case "playing":
                ShowHome();
                ShowPlaying(L("L'écran est à 60 Hz ; il revient à 100 Hz quand vous quittez le jeu.", "The display is at 60 Hz; it goes back to 100 Hz when you leave the game.", "A tela está a 60 Hz; ela volta para 100 Hz quando você sair do jogo."));
                break;
            default:
                ShowHome();
                ShowVersion("v0.6.1", what == "home-update" ? "v0.7.0" : "v0.6.1", true, true);
                if (what == "mods") { NavMods.IsChecked = true; ShowModsPage(preview: true); }
                if (what == "home-building")
                {
                    outPath = s.Folder;
                    updating = previewBuilding = true;
                    Show(View.Home);
                }
                if (what == "home-mods") ShowMods(preview: 2);
                if (what is "options" or "smooth" or "game" or "controls") NavOptions.IsChecked = true;
                if (what == "smooth") TabSmooth.IsChecked = true;
                else if (what == "game") TabGame.IsChecked = true;
                else if (what == "controls") TabControls.IsChecked = true;
                break;
        }
    }

    void TakeSnapshot(string path)
    {
        Dispatcher.BeginInvoke(() =>
        {
            UpdateLayout();
            var dpi = VisualTreeHelper.GetDpi(this);
            var bmp = new RenderTargetBitmap((int)(ActualWidth * dpi.DpiScaleX), (int)(ActualHeight * dpi.DpiScaleY), dpi.PixelsPerInchX, dpi.PixelsPerInchY, PixelFormats.Pbgra32);
            bmp.Render(Root);
            var enc = new PngBitmapEncoder();
            enc.Frames.Add(BitmapFrame.Create(bmp));
            using (var f = File.Create(path)) enc.Save(f);
            Close();
        }, DispatcherPriority.ApplicationIdle);
    }
}
