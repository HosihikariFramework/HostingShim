using Hosihikari;
using Hosihikari.Extension.Event;

[assembly: EntryPoint<Probe.ProbePlugin>]

namespace Probe;

/// <summary>投递探针：证明原生事件走到了插件，并记下视图取到的身份。</summary>
/// <remarks>
/// 订阅在 <c>OnLoad</c>，但要等 <c>OnEnable</c> 之后才有事件进来：作用域的闸门在
/// 启用成功后才打开，启用之前到期或到达的都被静默丢弃。脚本据此先等 <c>ENABLED</c>
/// 再让 bot 连入。
///
/// 记的是 <see cref="Hosihikari.Extension.Abstractions.IPlayerView"/> 的取值，不是
/// 原生对象本身的字段 —— 要验的正是投影视图这一层。
/// </remarks>
public sealed class ProbePlugin : IPlugin
{
    private static string _log = "";

    private static void L(string line)
    {
        try
        {
            File.AppendAllText(_log, line + "\n");
        }
        catch
        {
            // 探针写文件失败不该拖垮插件本身。
        }
    }

    public ValueTask OnLoad(IPluginContext context, CancellationToken token)
    {
        _log = Path.Combine(context.DataDirectory, "delivered.txt");
        context.Events.Subscribe<PlayerJoinEvent>(e =>
            L($"JOIN name=[{e.Player.Name}] xuid=[{e.Player.Xuid}] native=[{e.Player.NativeView?.GetType().Name ?? "null"}]")
        );
        context.Events.Subscribe<PlayerLeaveEvent>(e => L($"LEAVE name=[{e.Player.Name}]"));
        context.Events.Subscribe<PlayerChatEvent>(e => L($"CHAT name=[{e.Player.Name}] msg=[{e.Message}]"));
        return ValueTask.CompletedTask;
    }

    public ValueTask<PluginHookResult> OnEnable(CancellationToken token)
    {
        L("ENABLED");
        return ValueTask.FromResult(PluginHookResult.Allow());
    }

    public ValueTask<PluginHookResult> OnDisable(CancellationToken token) =>
        ValueTask.FromResult(PluginHookResult.Allow());

    public ValueTask OnUnload(CancellationToken token) => ValueTask.CompletedTask;
}
