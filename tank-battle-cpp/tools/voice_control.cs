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

    static void LoadGrammars(SpeechRecognitionEngine eng)
    {
        CultureInfo cult = eng.RecognizerInfo.Culture;
        eng.LoadGrammar(Mk(cult, "FIRE", new string[] { "开炮", "发射", "开火" }));
        eng.LoadGrammar(Mk(cult, "STOP", new string[] { "停", "停止" }));
        eng.LoadGrammar(Turret(cult, "TURRET_CW",
            new string[] { "顺时针", "顺时针旋转", "向右转", "右转" }));
        eng.LoadGrammar(Turret(cult, "TURRET_CCW",
            new string[] { "逆时针", "逆时针旋转", "向左转", "左转" }));
        eng.LoadGrammar(Mk(cult, "MOVE_UP", new string[] { "上移", "向上移动", "前进" }));
        eng.LoadGrammar(Mk(cult, "MOVE_DOWN", new string[] { "下移", "向下移动", "后退" }));
        eng.LoadGrammar(Mk(cult, "MOVE_LEFT", new string[] { "左移", "向左移动" }));
        eng.LoadGrammar(Mk(cult, "MOVE_RIGHT", new string[] { "右移", "向右移动" }));
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
        udp = new UdpClient();
        eng.SetInputToDefaultAudioDevice();
        eng.SpeechRecognized += OnSpeech;
        eng.RecognizeAsync(RecognizeMode.Multiple);
        Console.WriteLine("[voice] 识别器 " + eng.RecognizerInfo.Culture.Name +
                          ", 正在监听麦克风 -> 127.0.0.1:" + port + "; 游戏退出后自动关闭");
        // 心跳: 游戏每秒 PING 一次, 首次等待 15 秒(游戏启动加载素材), 之后 5 秒
        UdpClient hb = new UdpClient(port + 1);
        hb.Client.ReceiveTimeout = 15000;
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
        if (r.Confidence < 0.4f)
        {
            Console.WriteLine("[voice] (忽略 置信度" + r.Confidence.ToString("0.00") + ") " + r.Text);
            return;
        }
        string name = r.Grammar.Name;
        if (name == "TURRET_CW" || name == "TURRET_CCW")
        {
            int deg = 30;
            if (r.Semantics != null && r.Semantics.Value is int) deg = (int)r.Semantics.Value;
            Send(name + " " + deg);
        }
        else Send(name);
    }

    static void Send(string s)
    {
        Console.WriteLine("[voice] " + s);
        if (udp == null) return;
        try
        {
            byte[] b = Encoding.UTF8.GetBytes(s + "\n");
            udp.Send(b, b.Length, "127.0.0.1", port);
        }
        catch (Exception) { }
    }
}
