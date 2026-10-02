# Google Chrome 页面标记

此路径支持独立 **Google Chrome** app（`com.google.Chrome`）的普通窗口。嵌在其他 app 的浏览器不属于此 Chrome 路径。先完成 Input Monitoring 和 Screen Recording。

## Automation 与恢复入口

1. 在独立 Chrome 打开安全测试页。让 Chrome 经过一次真实的前台激活：例如先切到另一 app，再切回 Chrome。
2. 如果 SeeThis 对 Chrome 的 Automation 未授权或已拒绝，Inspector 中会显示 Chrome 恢复区，包括 **Connect Chrome**、**Retry Chrome** 和 **Automation Settings…**。它只在当前 Chrome 上下文需要权限恢复时显示。
3. 点 **Connect Chrome** 发起明确的 Automation 请求；如果系统显示 SeeThis 想控制 Google Chrome，核对名称后允许。已拒绝时，用 **Automation Settings…** 打开 **System Settings > Privacy & Security > Automation > SeeThis > Google Chrome**，由你允许对应开关，再点 **Retry Chrome**。
4. 回到原 Chrome 测试页，等待页面身份就绪，再按配置的 Capture 快捷键圈选。恢复区在权限就绪后隐藏；实际 mark 成功仍需圈选验证。

隐藏启动、仅显示 Inspector、非 Chrome app 和已经授权的 Chrome 不会主动触发这个恢复入口。后台观测不弹 Automation 请求。只是打开系统权限设置也不会建立新的请求条目；没有条目时，先真实激活 Chrome，再使用恢复区的 Connect Chrome。

拒绝/不可用、页面身份 ambiguous 或 timed out 都不是成功；该次圈选不应保存引用或复制 URL。保持正确的独立 Chrome 窗口前台，核对 **Location:** 是否运行已授权的那份 app，松开快捷键再重试。仍异常时可退出重复副本，手动重开确切 app，并记录准确状态。无需浏览器扩展或 Chrome 的 “Allow JavaScript from Apple Events” 选项。

## 页面隔离

在安全 tab A 创建 mark，切到 B 时，A 的实时 mark 应隐藏；切回 A 并等待新鲜页面观测时，未删除的 mark 应恢复。同 URL 的不同 tab 仍有不同身份。导航到其他页面会隐藏原 mark；回到原上下文时再恢复。切换 app/window 也受原始上下文约束。

Chrome 退出重开会改变进程身份，不能承诺旧 mark 在新进程自动显示；有效历史引用仍可从 Inspector 复制或预览。Inspector 历史选中项和显式复制操作，与当前页面实时 mark 的可交互性分别判断。

完整的 tab/reload/back/window、首次授权、重开和多系统矩阵没有逐项全部复测。页面隔离规则由实现和自动测试覆盖，不能把这些检查当作整个物理矩阵通过。反馈时使用安全测试内容，不公开真实 tab 地址、标题或引用读取 capability。
