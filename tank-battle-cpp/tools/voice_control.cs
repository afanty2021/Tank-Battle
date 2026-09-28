// voice_control.cs —— 语音控制助手(移植版新增, 仅 Windows)
// 用 System.Speech 离线识别中文口令, 经本机 UDP(默认 52017)发给游戏。
// 游戏每秒向 52018 发心跳, 失联(游戏退出)后本进程自动退出。
// 构建: build.bat 用 csc 编译(注意: 系统自带 csc 仅支持 C# 5 语法);
// 手动: %WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe /codepage:65001
//       /target:exe /r:System.Speech.dll /out:tools\voice_control.exe tools\voice_control.cs
// 离线自测(不经麦克风): tools\voice_control.exe --wav=xxx.wav
using System;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Speech.Recognition;
using System.Text;

class VoiceControl
{
    static int port = 52017;
    static UdpClient udp; // null = wav 测试模式(只打印不发送)

    static int Main(string[] args)
    {
        try { Console.OutputEncoding = Encoding.UTF8; } catch (Exception) { }
        foreach (string a in args)
        {
            if (a.StartsWith("--port="))
            {
                int p;
                if (int.TryParse(a.Substring(7), out p)) port = p;
            }
            else if (a.StartsWith("--wav=")) return RunWav(a.Substring(6));
        }
        return RunMic();
    }

    static SpeechRecognitionEngine CreateEngine()
    {
        foreach (RecognizerInfo ri in SpeechRecognitionEngine.InstalledRecognizers())
            if (ri.Culture != null && ri.Culture.Name.StartsWith("zh"))
                return new SpeechRecognitionEngine(ri);
        System.Collections.ObjectModel.ReadOnlyCollection<RecognizerInfo> all =
            SpeechRecognitionEngine.InstalledRecognizers();
        if (all.Count > 0) return new SpeechRecognitionEngine(all[0]);
        return null;
    }

    static Grammar Mk(CultureInfo c, string name, string[] words)
    {
        var gb = new GrammarBuilder(new Choices(words));
        gb.Culture = c;
        return new Grammar(gb) { Name = name };
    }

    static Grammar Turret(CultureInfo c, string name, string[] dirWords)
    {
        var gb = new GrammarBuilder();
        gb.Culture = c;
        gb.Append("炮塔", 0, 1);
        gb.Append(new Choices(dirWords));
        gb.Append(NumberChoices());
        gb.Append("度", 0, 1);
        return new Grammar(gb) { Name = name };
    }

    static Choices NumberChoices()
    {
        string[] words = { "一", "五", "十", "十五", "二十", "二十五",
                           "三十", "四十五", "六十", "九十", "一百八", "一百八十" };
        int[] vals = { 1, 5, 10, 15, 20, 25, 30, 45, 60, 90, 180, 180 };
        var c = new Choices();
        for (int i = 0; i < words.Length; i++)
            c.Add(new SemanticResultValue(words[i], vals[i]));
        return c;
    }

    static Grammar Move(CultureInfo c)
    {
        // 移动别名: 前进/后退/退后(不含上/下字样, 声学最稳的一组, 实测
        // 0.74~0.85)。四向本体(上移/下移/左移/右移)是 LoadGrammars 里的
        // 四个独占小语法(0.99)。
        // 引擎对 上/下 两字声学区分力弱: 任何"向上/向下"多字组合(向上
        // 移动/往下走/向下...)实测置信度只有 0.3~0.6 且互抢翻转(用户实测
        // "向下移动"被听成"向上移动"), 一律不收——这些说法只会在 0.65
        // 阈值下被安全忽略, 绝不会反向行驶。组合越多还会摊薄别的路径
        // (加 8 条二字口令后"前进"就被 STOP 语法抢走, 实测), 故保持最小
        var b = new Choices();
        b.Add(new SemanticResultValue("前进", 1));
        b.Add(new SemanticResultValue("后退", 2));
        b.Add(new SemanticResultValue("退后", 2));
        var bare = new GrammarBuilder();
        bare.Culture = c;
        bare.Append(b);
        return new Grammar(bare) { Name = "MOVE" };
    }

    static void LoadGrammars(SpeechRecognitionEngine eng)
    {
        CultureInfo cult = eng.RecognizerInfo.Culture;
        eng.LoadGrammar(Mk(cult, "FIRE", new string[] { "开炮", "发射", "开火" }));
        eng.LoadGrammar(Mk(cult, "STOP", new string[] { "停", "停止" }));
        eng.LoadGrammar(Turret(cult, "TURRET_CW",
            new string[] { "顺时针", "顺时针转", "顺时针旋转",
                           "向右转", "向右旋转", "右转" }));
        eng.LoadGrammar(Turret(cult, "TURRET_CCW",
            new string[] { "逆时针", "逆时针转", "逆时针旋转",
                           "向左转", "向左旋转", "左转" }));
        // 短口令独占小语法: 每个只有一条路径, 干净语音置信度 0.99
        eng.LoadGrammar(Mk(cult, "MOVE_UP", new string[] { "上移" }));
        eng.LoadGrammar(Mk(cult, "MOVE_DOWN", new string[] { "下移" }));
        eng.LoadGrammar(Mk(cult, "MOVE_LEFT", new string[] { "左移" }));
        eng.LoadGrammar(Mk(cult, "MOVE_RIGHT", new string[] { "右移" }));
        eng.LoadGrammar(Move(cult));
    }

    static int RunWav(string path)
    {
        SpeechRecognitionEngine eng = CreateEngine();
        if (eng == null) { Console.WriteLine("[voice] no recognizer installed"); return 2; }
        LoadGrammars(eng);
        eng.SetInputToWaveFile(path);
        RecognitionResult r;
        int n = 0;
        while ((r = eng.Recognize()) != null) { OnRecog(r); n++; }
        Console.WriteLine("[voice] wav done, results=" + n);
        return 0;
    }

    static int RunMic()
    {
        SpeechRecognitionEngine eng = CreateEngine();
        if (eng == null)
        {
            Console.WriteLine("[voice] 未找到语音识别器(需在 Windows 设置里安装中文语音识别)");
            return 2;
        }
        LoadGrammars(eng);
        // 心跳: 游戏每秒 PING 一次, 首次等待 15 秒(游戏启动加载素材), 之后 5 秒。
        // UdpClient(port) 构造即绑定端口。快速重启竞态: 上一局退出后旧助手要
        // ~5 秒才超时退出并释放 52018, 这期间启动的新助手会绑定失败——若立即
        // 放弃, 新会话语音就静默失效, 故重试等待。绑定放在开麦之前: 若旧助手
        // 被新游戏的心跳喂着一直活着(由它继续服务), 新助手等不到端口就直接
        // 退出, 避免两个助手同时开麦识别出双份命令
        UdpClient hb = null;
        for (int i = 0; hb == null; i++)
        {
            try { hb = new UdpClient(port + 1); }
            catch (Exception)
            {
                if (i == 0)
                    Console.WriteLine("[voice] 心跳端口 " + (port + 1) +
                                      " 被占用, 等待旧助手退出(最多 8 秒)...");
                if (i >= 16) // 17 次尝试 x 0.5 秒 ~= 8 秒
                {
                    Console.WriteLine("[voice] 心跳端口仍被占用(已有助手在服务?), 退出");
                    return 2;
                }
                System.Threading.Thread.Sleep(500);
            }
        }
        hb.Client.ReceiveTimeout = 15000;
        udp = new UdpClient();
        eng.SetInputToDefaultAudioDevice();
        eng.SpeechRecognized += OnSpeech;
        eng.RecognizeAsync(RecognizeMode.Multiple);
        Console.WriteLine("[voice] 识别器 " + eng.RecognizerInfo.Culture.Name +
                          ", 正在监听麦克风 -> 127.0.0.1:" + port + "; 游戏退出后自动关闭");
        while (true)
        {
            try
            {
                IPEndPoint ep = null;
                hb.Receive(ref ep);
                hb.Client.ReceiveTimeout = 5000;
            }
            catch (SocketException)
            {
                Console.WriteLine("[voice] 游戏心跳超时, 退出");
                return 0;
            }
        }
    }

    static void OnSpeech(object s, SpeechRecognizedEventArgs e) { OnRecog(e.Result); }

    static void OnRecog(RecognitionResult r)
    {
        // 阈值分档: FIRE/STOP 误识别代价小(多打一发/停一下), 0.4 即可;
        // MOVE*/TURRET* 误识别代价大(反向行驶/炮塔乱转), 0.65 起才执行
        // (劣化音频的错误识别实测集中在 0.50~0.59, 干净语音全在 0.9+)
        string name = r.Grammar.Name;
        bool lasting = name.StartsWith("MOVE") ||
                       name == "TURRET_CW" || name == "TURRET_CCW";
        float thr = lasting ? 0.65f : 0.4f;
        if (r.Confidence < thr)
        {
            Console.WriteLine("[voice] (忽略 置信度" + r.Confidence.ToString("0.00") + ") " + r.Text);
            return;
        }
        // 打印听到的原文+置信度(只进控制台日志); UDP 仍只发命令串本身,
        // 游戏端 parse() 是精确匹配, 带附加文本会解析失败
        string heard = " (听到:\"" + r.Text + "\" 置信度" + r.Confidence.ToString("0.00") + ")";
        if (name == "MOVE")
        {
            // 语义值 1~4 = 上/下/左/右; 缺语义不猜方向, 直接丢弃
            if (r.Semantics == null || !(r.Semantics.Value is int)) return;
            int d = (int)r.Semantics.Value;
            string[] cmds = { null, "MOVE_UP", "MOVE_DOWN", "MOVE_LEFT", "MOVE_RIGHT" };
            if (d < 1 || d > 4) return;
            Send(cmds[d], heard);
        }
        else if (name == "TURRET_CW" || name == "TURRET_CCW")
        {
            int deg = 30;
            if (r.Semantics != null && r.Semantics.Value is int) deg = (int)r.Semantics.Value;
            Send(name + " " + deg, heard);
        }
        else Send(name, heard);
    }

    static void Send(string cmd, string log)
    {
        Console.WriteLine("[voice] " + cmd + log);
        if (udp == null) return;
        try
        {
            byte[] b = Encoding.UTF8.GetBytes(cmd + "\n");
            udp.Send(b, b.Length, "127.0.0.1", port);
        }
        catch (Exception) { }
    }
}
