using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using static ReLink.Texts;

namespace ReLink;

/// (relink-amd) The Saves page: ReLink's own saves, and the saves of the game that Longshot, Triforce, the emulators and NX2navi
/// keep on this PC (Saves.cs), each with an Import button that copies it over ReLink's, ReLink's backed up first
public partial class MainWindow
{
    List<SaveSet> foundSaves = new();
    SaveSet? chosenSaves;   // (one picked with Choose a folder)
    bool savesBusy, savesScanned;

    void SavesTexts()
    {
        NavSavesText.Text = L("Sauvegardes", "Saves", "Saves");
        SavesTitle.Text = L("Importer des sauvegardes", "Import saves", "Importar saves");
        SavesIntro.Text = L("Longshot, Triforce, les émulateurs (Eden, Lichii, Eglantine, Ryujinx...) et NX2navi gardent les sauvegardes du jeu dans les mêmes fichiers que ReLink : les importer est une simple copie, sans conversion. Les sauvegardes actuelles de ReLink sont copiées dans save-backups avant.",
                            "Longshot, Triforce, the emulators (Eden, Lichii, Eglantine, Ryujinx...) and NX2navi keep the game's saves in the same files as ReLink: importing one is a plain copy, nothing is converted. ReLink's current saves are copied to save-backups first.",
                            "O Longshot, o Triforce, os emuladores (Eden, Lichii, Eglantine, Ryujinx...) e o NX2navi guardam os saves do jogo nos mesmos arquivos que o ReLink: importar é só uma cópia, sem conversão. Os saves atuais do ReLink são copiados para save-backups antes.");
        SavesKeepText.Text = L("Garder mes réglages du jeu (option.sav)", "Keep my game settings (option.sav)", "Manter as minhas configurações do jogo (option.sav)");
        SavesBrowse.Content = L("Choisir un dossier...", "Choose a folder...", "Escolher pasta...");
        SavesRescan.Content = L("Chercher à nouveau", "Look again", "Procurar de novo");
        SavesOpenBackups.Content = L("Ouvrir les sauvegardes de secours", "Open the backups", "Abrir os backups");
    }

    string SlotsText(SaveSet s) => s.Slots == 1 ? L("1 emplacement", "1 slot", "1 slot") : L($"{s.Slots} emplacements", $"{s.Slots} slots", $"{s.Slots} slots");
    string WhenText(DateTime d) => d.ToString(L("d MMM yyyy, HH:mm", "d MMM yyyy, HH:mm", "dd/MM/yyyy, HH:mm"));

    /// The page: ReLink's saves on top, the ones found under them (scanned once, again on Look again)
    async void ShowSavesPage(bool rescan = false, bool preview = false)
    {
        SavesMine.Text = MineText();
        if (preview)
        {
            foundSaves = new List<SaveSet>
            {
                new() { Name = @"Triforce (C:\Users\Player\Games\Triforce)", Folder = @"C:\Users\Player\Games\Triforce\user\save\" + Saves.Uid, Slots = 3, Last = DateTime.Now.AddHours(-2) },
                new() { Name = @"Longshot (D:\Games\Longshot)", Folder = @"D:\Games\Longshot\user\save\" + Saves.Uid, Slots = 1, Last = DateTime.Now.AddDays(-3) },
                new() { Name = "Eden / Lichii / Orchid", Folder = @"C:\Users\Player\AppData\Roaming\eden\nand\user\save\0000000000000000\8E2C\0400486024DEC000", Slots = 2, Last = DateTime.Now.AddDays(-9) },
            };
            FillSaves(preview: true);
            return;
        }
        if (savesScanned && !rescan) { FillSaves(); return; }
        if (savesBusy) return;
        savesBusy = true;
        SavesList.Children.Clear();
        SavesNote(L("Recherche des sauvegardes d'Ocarina of Time sur ce PC...", "Looking for Ocarina of Time saves on this PC...", "Procurando saves do Ocarina of Time neste PC..."));
        var exclude = s.Folder;
        try { foundSaves = await Task.Run(() => Saves.FindAll(exclude)); }
        catch (Exception) { foundSaves = new List<SaveSet>(); }
        savesBusy = false;
        savesScanned = true;
        SavesNote("");
        FillSaves();
    }

    string MineText()
    {
        if (s.Folder == "" || !Game.Installed(s.Folder))
            return L("Installez d'abord le jeu : les sauvegardes de ReLink sont dans son dossier.", "Install the game first: ReLink's saves are in its folder.", "Instale o jogo primeiro: os saves do ReLink ficam na pasta dele.");
        var mine = Saves.Describe("ReLink", Saves.Folder(s.Folder));
        return mine.Empty ? L("ReLink n'a pas encore de sauvegarde.", "ReLink has no saves yet.", "O ReLink ainda não tem saves.")
                          : L("ReLink a maintenant ", "ReLink now has ", "O ReLink tem agora ") + SlotsText(mine) + " · " +
                            L("dernière le ", "last saved ", "último save em ") + WhenText(mine.Last);
    }

    void FillSaves(bool preview = false)
    {
        SavesList.Children.Clear();
        var items = new List<SaveSet>(foundSaves);
        if (chosenSaves != null && items.All(x => !string.Equals(x.Folder, chosenSaves.Folder, StringComparison.OrdinalIgnoreCase))) items.Insert(0, chosenSaves);
        if (items.Count == 0)
            SavesList.Children.Add(new TextBlock
            {
                Text = L("Aucune sauvegarde d'un autre programme trouvée. Utilisez Choisir un dossier : le dossier du jeu de Longshot ou de Triforce, ou celui qui contient option.sav et slot_00 à slot_05.",
                         "No saves from another program were found. Use Choose a folder: Longshot's or Triforce's game folder, or the folder that holds option.sav and slot_00 to slot_05.",
                         "Nenhum save de outro programa foi encontrado. Use Escolher pasta: a pasta do jogo do Longshot ou do Triforce, ou a pasta que tem option.sav e slot_00 a slot_05."),
                Style = St("Body"), Margin = new Thickness(2, 6, 0, 0),
            });
        var canImport = !preview && s.Folder != "" && Game.Installed(s.Folder) && game == null && runner == null;
        foreach (var set in items)
        {
            var label = new StackPanel
            {
                Children =
                {
                    new TextBlock { Text = set.Name, Style = St("RowLabel"), TextTrimming = TextTrimming.CharacterEllipsis },
                    new TextBlock { Text = SlotsText(set) + " · " + L("dernière le ", "last saved ", "último save em ") + WhenText(set.Last), Style = St("RowHint") },
                    new TextBlock { Text = set.Folder, Style = St("RowHint"), FontSize = 11.5, Foreground = (Brush)FindResource("BoneFaint"), TextTrimming = TextTrimming.CharacterEllipsis, TextWrapping = TextWrapping.NoWrap },
                },
            };
            var go = new Button
            {
                Content = L("Importer", "Import", "Importar"), Style = St("Link"), Foreground = (Brush)FindResource("Blue"),
                VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(16, 0, 0, 0), IsEnabled = canImport,
            };
            var entry = set;
            go.Click += (_, _) => ImportSaves(entry);
            var row = new Grid { ColumnDefinitions = { new ColumnDefinition(), new ColumnDefinition { Width = GridLength.Auto } } };
            row.Children.Add(label);
            Grid.SetColumn(go, 1);
            row.Children.Add(go);
            SavesList.Children.Add(new Border { Style = St("Row"), Child = row });
        }
        if (!preview && (game != null || runner != null))
            SavesNote(L("Fermez le jeu (ou attendez la fin de la construction) pour importer.", "Close the game (or wait for the build to end) to import.", "Feche o jogo (ou espere a compilação terminar) para importar."));
    }

    void SavesNote(string text, bool bad = false, bool good = false)
    {
        SavesStatus.Text = text;
        SavesStatus.Visibility = text == "" ? Visibility.Collapsed : Visibility.Visible;
        SavesStatus.Foreground = (Brush)FindResource(bad ? "Warn" : good ? "Green" : "HelpInk");
    }

    void SavesBrowseClick(object sender, RoutedEventArgs e)
    {
        var dir = PickFolder(L("Choisir le dossier des sauvegardes (ou le dossier du jeu de Longshot ou Triforce)",
                               "Choose the saves' folder (or Longshot's or Triforce's game folder)",
                               "Escolha a pasta dos saves (ou a pasta do jogo do Longshot ou do Triforce)"));
        if (dir == null) return;
        var set = Saves.FindIn(dir);
        if (set == null)
        {
            SavesNote(L($"Aucune sauvegarde de ce jeu dans {dir}. Choisissez le dossier qui contient option.sav et slot_00 à slot_05.",
                        $"No saves of this game in {dir}. Choose the folder that holds option.sav and slot_00 to slot_05.",
                        $"Nenhum save deste jogo em {dir}. Escolha a pasta que tem option.sav e slot_00 a slot_05."), bad: true);
            return;
        }
        chosenSaves = Saves.Describe(Path.GetFileName(dir.TrimEnd('\\')) + L(" (choisi)", " (chosen)", " (escolhida)"), set);
        SavesNote("");
        FillSaves();
    }

    void SavesRescanClick(object sender, RoutedEventArgs e) { chosenSaves = null; ShowSavesPage(rescan: true); }

    void SavesOpenBackupsClick(object sender, RoutedEventArgs e)
    {
        if (s.Folder == "") return;
        var root = Saves.BackupRoot(s.Folder);
        try { Directory.CreateDirectory(root); } catch (Exception) { }
        Open(root);
    }

    async void ImportSaves(SaveSet from)
    {
        if (savesBusy || s.Folder == "" || !Game.Installed(s.Folder) || game != null || runner != null) return;
        if (Saves.RunningGame() is { } running)
        {
            SavesNote(L($"Fermez d'abord {running} : il réécrirait ses propres sauvegardes en se fermant.", $"Close {running} first: it would write its own saves back when it closes.",
                        $"Feche o {running} primeiro: ele gravaria os próprios saves de novo ao fechar."), bad: true);
            return;
        }
        var mine = Saves.Describe("ReLink", Saves.Folder(s.Folder));
        var ask = L($"Remplacer les sauvegardes de ReLink par celles de {from.Name} ({SlotsText(from)}) ?", $"Replace ReLink's saves with {from.Name}'s ({SlotsText(from)})?",
                    $"Trocar os saves do ReLink pelos do {from.Name} ({SlotsText(from)})?") +
                  (mine.Empty ? "" : "\n\n" + L($"Les sauvegardes actuelles de ReLink ({SlotsText(mine)}) sont copiées dans save-backups avant.",
                                               $"ReLink's current saves ({SlotsText(mine)}) are copied to save-backups first.",
                                               $"Os saves atuais do ReLink ({SlotsText(mine)}) são copiados para save-backups antes."));
        if (MessageBox.Show(this, ask, L("Importer des sauvegardes", "Import saves", "Importar saves"), MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        savesBusy = true;
        SavesNote(L("Copie...", "Copying...", "Copiando..."));
        var to = Saves.Folder(s.Folder);
        var backups = Saves.BackupRoot(s.Folder);
        var keep = SavesKeep.IsChecked == true;
        try
        {
            var backup = await Task.Run(() => Saves.Import(from.Folder, to, backups, "before " + from.Name, keep));
            var now = Saves.Describe("ReLink", to);
            savesBusy = false;
            SavesMine.Text = MineText();
            FillSaves();
            SavesNote(L($"Terminé : {SlotsText(now)} importés de {from.Name}.", $"Done: {SlotsText(now)} imported from {from.Name}.", $"Pronto: {SlotsText(now)} importados do {from.Name}.") +
                      (backup != null ? " " + L("Les anciennes sont dans ", "The old ones are in ", "Os antigos estão em ") + backup : ""), good: true);
        }
        catch (Exception ex)
        {
            savesBusy = false;
            SavesNote(L("Sauvegardes non copiées : ", "The saves were not copied: ", "Os saves não foram copiados: ") + ex.Message, bad: true);
        }
    }
}
