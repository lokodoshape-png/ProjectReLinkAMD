// (relink-amd) Saves of this game (Ocarina of Time, 0400486024DEC000) kept by other programs, found to be brought into ReLink.
// ReLink, Longshot, Triforce and Hookshot (the port all three come from) keep them in <game folder>\user\save\<uid>; the emulators
// that run the game (Eden and its forks Lichii, Orchid, PokeDinner; Eglantine; yuzu-likes) in their NAND
// (nand\user\save\0000000000000000\<user>\0400486024DEC000), Ryujinx-likes in bis\user\save\<index>\0, NX2navi in its game
// folder's profile\save\<account>. The files are the same everywhere (account.sav, option.sav, slot_00..05\caption.sav +
// progress.sav), so importing is a copy, with what gets replaced backed up first.
// Adapted from Triforce's setup/Saves.cs (GPL-3.0-or-later, the Triforce contributors), whose docs/saves.md lists the sources.
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace ReLink;

sealed class SaveSet
{
    public string Name = "", Folder = "";
    public int Slots;
    public DateTime Last;
    public bool Empty;
}

static class Saves
{
    public const string TitleId = "0400486024DEC000";
    /// The save folder's name: host/nn_applet.c user_uid, host/nn_fs.c save\<uid hi><uid lo> (the same in Longshot, Triforce, Hookshot)
    public const string Uid = "5a3c9e1f0b7d24680000000000000001";
    static readonly string[] Emulators = { "eden", "eglantine", "EvilOOT", "yuzu", "suyu", "sudachi", "citron", "torzu", "Ryujinx", "Ryubing" };
    /// The ports of the same code: a folder's name or its game's program tells which one it is
    static readonly string[] Ports = { "Triforce", "Longshot", "Hookshot", "ReLink" };

    public static string Folder(string game) => Path.Combine(game, "user", "save", Uid);
    public static string BackupRoot(string game) => Path.Combine(game, "save-backups");

    public static bool IsSaveSet(string dir) =>
        Directory.Exists(dir) && (File.Exists(Path.Combine(dir, "option.sav")) || File.Exists(Path.Combine(dir, "account.sav"))
                                  || File.Exists(Path.Combine(dir, "slot_00", "progress.sav")));

    public static SaveSet Describe(string name, string dir)
    {
        var s = new SaveSet { Name = name, Folder = dir };
        try
        {
            if (!Directory.Exists(dir)) { s.Empty = true; return s; }
            s.Slots = Directory.GetDirectories(dir, "slot_*").Count(d => File.Exists(Path.Combine(d, "progress.sav")));
            var files = Directory.GetFiles(dir, "*.sav", SearchOption.AllDirectories);
            s.Empty = files.Length == 0;
            s.Last = files.Length > 0 ? files.Max(f => File.GetLastWriteTime(f)) : DateTime.MinValue;
        }
        catch (Exception) { s.Empty = true; }
        return s;
    }

    static IEnumerable<string> SafeDirs(string d)
    {
        try { return Directory.GetDirectories(d); } catch (Exception) { return Array.Empty<string>(); }
    }

    static string Norm(string p)
    {
        try { return Path.GetFullPath(p).TrimEnd('\\'); } catch (Exception) { return p; }
    }

    /// Which port a game folder is, by the programs in it and its name (null: none of them)
    static string? PortName(string game)
    {
        string[] exes;
        try { exes = Directory.GetFiles(game, "*.exe").Concat(Directory.Exists(Path.Combine(game, "bin")) ? Directory.GetFiles(Path.Combine(game, "bin"), "*.exe") : Array.Empty<string>()).ToArray(); }
        catch (Exception) { exes = Array.Empty<string>(); }
        foreach (var p in Ports)
            if (exes.Any(e => Path.GetFileName(e).Contains(p, StringComparison.OrdinalIgnoreCase))) return p;
        var path = game.Replace('/', '\\');
        foreach (var p in Ports)
            if (path.Contains(p, StringComparison.OrdinalIgnoreCase)) return p;
        return null;
    }

    /// The save NX2navi would load, given its game folder: <game>\profile\save\<account>, or a finished commit in
    /// <game>\profile\save-tx\ready-<n> (the highest n), which is newer until the game's next start swaps it in
    public static string? NaviSave(string dir)
    {
        try
        {
            foreach (var profile in new[] { Path.Combine(dir, "profile"), dir })
            {
                var saves = Path.Combine(profile, "save");
                if (!Directory.Exists(saves)) continue;
                var tx = Path.Combine(profile, "save-tx");
                if (Directory.Exists(tx))
                {
                    var ready = Directory.GetDirectories(tx, "ready-*")
                        .Select(d => (Dir: d, M: Regex.Match(Path.GetFileName(d), @"^ready-(\d{1,10})$")))
                        .Where(r => r.M.Success && IsSaveSet(r.Dir))
                        .OrderByDescending(r => long.Parse(r.M.Groups[1].Value)).FirstOrDefault();
                    if (ready.Dir != null) return ready.Dir;
                }
                var account = Directory.GetDirectories(saves)
                    .Where(d => Regex.IsMatch(Path.GetFileName(d), "^[0-9a-fA-F]{32}$") && IsSaveSet(d))
                    .OrderByDescending(d => Describe("", d).Last).FirstOrDefault();
                if (account != null) return account;
            }
        }
        catch (Exception) { }
        return null;
    }

    /// Every save of this game found on the PC but ReLink's own (exclude: ReLink's game folder): Triforce's game folder (its
    /// setup's HKCU\Software\Triforce GameFolder), the usual game folders (<home>\Games\<name>, <drive>:\Games\<name>), the
    /// folders the desktop and Start menu shortcuts start, the emulators' data folders and NX2navi's games
    public static List<SaveSet> FindAll(string? exclude)
    {
        var found = new List<SaveSet>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (exclude is { Length: > 0 }) seen.Add(Norm(Folder(exclude)));
        void Add(string name, string dir)
        {
            try
            {
                if (!IsSaveSet(dir)) return;
                var full = Norm(dir);
                if (seen.Add(full)) found.Add(Describe(name, full));
            }
            catch (Exception) { }
        }
        // (a port's game folder: user\save\<any 32 hex digits>, its own uid first)
        void Port(string game, string? name = null)
        {
            var root = Path.Combine(game, "user", "save");
            if (!Directory.Exists(root)) return;
            var label = (name ?? PortName(game) ?? Path.GetFileName(game.TrimEnd('\\'))) + " (" + game.TrimEnd('\\') + ")";
            Add(label, Path.Combine(root, Uid));
            foreach (var d in SafeDirs(root))
                if (Regex.IsMatch(Path.GetFileName(d), "^[0-9a-fA-F]{32}$")) Add(label, d);
        }
        // (a folder that may be a port's game folder, or hold one in "game" as ReLink's install does, or one level below)
        void PortTree(string dir, string? name = null)
        {
            if (!Directory.Exists(dir)) return;
            Port(dir, name);
            Port(Path.Combine(dir, "game"), name);
            var parent = Path.GetFileName(dir.TrimEnd('\\'));
            if (parent.Equals("bin", StringComparison.OrdinalIgnoreCase) && Path.GetDirectoryName(dir) is { } up) Port(up, name);
        }
        void Nand(string name, string root)   // (root\nand\user\save\0000000000000000\<user>\<title>, and newer Eden's account\<user>\<title>\0)
        {
            var users = Path.Combine(root, "nand", "user", "save", "0000000000000000");
            foreach (var u in SafeDirs(users)) Add(name, Path.Combine(u, TitleId));
            var accounts = Path.Combine(root, "nand", "user", "save", "account");
            foreach (var u in SafeDirs(accounts)) Add(name, Path.Combine(u, TitleId, "0"));
        }
        void Ryujinx(string name, string root)   // (root\bis\user\save\<index>\0)
        {
            foreach (var s in SafeDirs(Path.Combine(root, "bis", "user", "save"))) Add(name, Path.Combine(s, "0"));
        }
        void Navi(string game)
        {
            if (NaviSave(game) is { } save) Add("NX2navi (" + game.TrimEnd('\\') + ")", save);
        }

        var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        var appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);

        // Triforce: its setup keeps the game folder it built in the registry
        foreach (var key in new[] { @"Software\Triforce", @"Software\Longshot", @"Software\Hookshot" })
        {
            try
            {
                using var k = Registry.CurrentUser.OpenSubKey(key);
                if (k?.GetValue("GameFolder") is string g && g.Length > 2) PortTree(g, key[(key.LastIndexOf('\\') + 1)..]);
            }
            catch (Exception) { }
        }
        // The usual places: <home>\Games\*, <drive>:\Games\*, the Programs folder, and the named folders first
        var gameRoots = new List<string> { Path.Combine(home, "Games") };
        try
        {
            foreach (var drive in DriveInfo.GetDrives())
                try { if (drive.DriveType == DriveType.Fixed && drive.IsReady) gameRoots.Add(Path.Combine(drive.RootDirectory.FullName, "Games")); }
                catch (Exception) { }
        }
        catch (Exception) { }
        foreach (var p in Ports.Where(p => p != "ReLink"))
        {
            foreach (var r in gameRoots) PortTree(Path.Combine(r, p), p);
            PortTree(Path.Combine(local, "Programs", p), p);
            PortTree(Path.Combine(local, p), p);
            PortTree(Path.Combine(home, p), p);
            PortTree(Path.Combine(home, "Desktop", p), p);
            PortTree(Path.Combine(home, "Downloads", p), p);
        }
        foreach (var r in gameRoots)
            foreach (var d in SafeDirs(r))
            {
                PortTree(d);
                foreach (var dd in SafeDirs(d)) Port(dd);
            }
        // NX2navi
        Navi(Path.Combine(local, "Programs", "NX2navi", "game"));
        foreach (var r in gameRoots) { Navi(Path.Combine(r, "NX2navi")); Navi(Path.Combine(r, "Ocarina")); }
        // The emulators' own data folders
        foreach (var e in Emulators)
        {
            var root = Path.Combine(appData, e);
            if (e.StartsWith("Ryu")) Ryujinx(e, root); else Nand(Pretty(e), root);
        }
        // Programs the shortcuts start: a port's game, a portable emulator (a "user" or "portable" folder beside it), NX2navi
        foreach (var exe in ShortcutTargets())
        {
            var dir = Path.GetDirectoryName(exe);
            if (dir == null) continue;
            PortTree(dir);
            var name = Path.GetFileNameWithoutExtension(exe) + " (" + dir + ")";
            Nand(name, Path.Combine(dir, "user"));
            Ryujinx(name, Path.Combine(dir, "portable"));
            Navi(dir);
            Navi(Path.Combine(dir, "game"));
        }
        return found.OrderByDescending(s => s.Last).ToList();
    }

    static string Pretty(string e) => e switch
    {
        "eden" => "Eden / Lichii / Orchid", "eglantine" => "Eglantine", "yuzu" => "yuzu", "suyu" => "suyu", "sudachi" => "Sudachi",
        "citron" => "Citron", "torzu" => "torzu", _ => e,
    };

    /// The .exe files the desktop and Start menu shortcuts start
    static IEnumerable<string> ShortcutTargets()
    {
        var places = new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Environment.GetFolderPath(Environment.SpecialFolder.CommonDesktopDirectory),
            Environment.GetFolderPath(Environment.SpecialFolder.Programs), Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms),
        };
        var exes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var type = Type.GetTypeFromProgID("WScript.Shell");
        if (type == null) return exes;
        object? shell = null;
        try
        {
            shell = Activator.CreateInstance(type);
            foreach (var p in places.Where(p => p != "" && Directory.Exists(p)))
            {
                IEnumerable<string> links;
                try { links = Directory.GetFiles(p, "*.lnk", p.EndsWith("Programs", StringComparison.OrdinalIgnoreCase) ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly); }
                catch (Exception) { continue; }
                foreach (var l in links.Take(400))
                {
                    try
                    {
                        var lnk = type.InvokeMember("CreateShortcut", System.Reflection.BindingFlags.InvokeMethod, null, shell, new object[] { l })!;
                        var target = lnk.GetType().InvokeMember("TargetPath", System.Reflection.BindingFlags.GetProperty, null, lnk, null) as string;
                        System.Runtime.InteropServices.Marshal.FinalReleaseComObject(lnk);
                        if (target != null && target.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && File.Exists(target)) exes.Add(target);
                    }
                    catch (Exception) { }
                }
            }
        }
        catch (Exception) { }
        finally { if (shell != null) System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell); }
        return exes;
    }

    /// A chosen folder: the save set it is, a port's or NX2navi's game folder's, or the first one inside it (a few levels down)
    public static string? FindIn(string dir)
    {
        if (IsSaveSet(dir)) return dir;
        foreach (var g in new[] { dir, Path.Combine(dir, "game") })
            if (IsSaveSet(Path.Combine(g, "user", "save", Uid))) return Path.Combine(g, "user", "save", Uid);
        if (NaviSave(dir) is { } navi) return navi;
        var queue = new Queue<(string, int)>();
        queue.Enqueue((dir, 0));
        var looked = 0;
        while (queue.Count > 0 && looked++ < 4000)
        {
            var (d, depth) = queue.Dequeue();
            foreach (var s in SafeDirs(d))
            {
                if (IsSaveSet(s)) return s;
                if (depth < 7) queue.Enqueue((s, depth + 1));
            }
        }
        return null;
    }

    /// A game or emulator that may have these saves open: it would write over the copy when it closes
    public static string? RunningGame()
    {
        foreach (var n in new[] { "relink-game", "Triforce", "Longshot", "longshot-game", "hookshot", "eden", "Lichii", "Eglantine", "EvilOOT", "yuzu", "suyu",
                                  "sudachi", "citron", "torzu", "Ryujinx", "ocarina" })
        {
            var ps = Process.GetProcessesByName(n);
            foreach (var p in ps) p.Dispose();
            if (ps.Length > 0) return n == "ocarina" ? "NX2navi" : n;
        }
        return null;
    }

    /// Copies a save set over ReLink's: ReLink's old files are copied to the backup folder first, then replaced (nothing is
    /// merged slot by slot: the copy is exactly the source). keepOptions: ReLink's own option.sav (the game's settings) stays.
    /// Returns the backup folder, or null when there was nothing to back up.
    public static string? Import(string from, string to, string backupRoot, string label, bool keepOptions)
    {
        var src = Norm(from);
        var dst = Norm(to);
        if (string.Equals(src, dst, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException(Texts.L("ce sont déjà les sauvegardes de ReLink", "those are ReLink's own saves", "esses já são os saves do próprio ReLink"));
        string? backup = null;
        byte[]? options = null;
        if (Directory.Exists(dst) && Directory.EnumerateFileSystemEntries(dst).Any())
        {
            var safe = string.Concat(label.Select(c => Path.GetInvalidFileNameChars().Contains(c) || c == ' ' ? '-' : c));
            if (safe.Length > 60) safe = safe[..60];
            backup = Path.Combine(backupRoot, DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss") + "_" + safe);
            CopyTree(dst, backup);
            var opt = Path.Combine(dst, "option.sav");
            if (keepOptions && File.Exists(opt)) options = File.ReadAllBytes(opt);
            Directory.Delete(dst, true);
        }
        CopyTree(src, dst);
        if (options != null) File.WriteAllBytes(Path.Combine(dst, "option.sav"), options);
        return backup;
    }

    static void CopyTree(string from, string to)
    {
        Directory.CreateDirectory(to);
        foreach (var f in Directory.GetFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), true);
        foreach (var d in Directory.GetDirectories(from)) CopyTree(d, Path.Combine(to, Path.GetFileName(d)));
    }
}
