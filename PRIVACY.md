# SeeThis 隐私说明

SeeThis 在本机处理屏幕引用。当前版本没有云同步、遥测上传或后台外发截图入口。你在聊天中粘贴/附加图片或允许同机工具读取 URL 后，接收方的保存与处理由该工具或服务决定。

## 捕获与权限

- **Input Monitoring** 观察全局快捷键、鼠标和中断；原生监听不记录无关按键内容。**Screen Recording** 捕获所选显示器，两项权限分别判断。
- 引用保存所选显示器的完整静态画面，以及区域/路径、来源 app/window、时间和尺寸。圈选区域不是隐私裁剪边界，截图前隐藏私人窗口和通知，分享前检查整幅图像。
- Chrome Automation 通过 Apple Events 读取前台独立 Chrome 的 tab ID 与 URL，生成页面身份摘要。公开元数据和日志应避免原 URL/tab token；截图像素仍可能包含它们。

## 保存、删除与失效

数据位于 `~/Library/Application Support/SeeThis`：`settings-v1.json` 保存设置，`references-v1` 保存引用与图像。默认保留期 7 天、最多 500 条可检索引用，可在 **Open Settings… > Reference access** 修改。保留从接受时间计算，app 的 prune 流程负责清理，退出后不会按钟点持续执行清理。

删除先写 tombstone、撤销本机读取并清理资产。可以保留无图像的删除/过期状态；清理失败时可能有残留，app 显示失败结果。这不是安全擦除，不会撤回备份、剪贴板副本或已发送附件。卸载 app 保留 Application Support 数据；自行移除数据的步骤见[卸载](docs/TROUBLESHOOTING.md#卸载与保留数据)。完整卸载/残留矩阵尚未复测。

## 链接与反馈

引用 URL 是读取 capability，知道完整链接的同机程序可读取授权内容。服务绑定 loopback `127.0.0.1`；远程助手需要你显式附加图片。不要公开完整引用 URL、详情 fragment、设置访问地址，或通过端口转发暴露本机服务。

服务有认证、失效/删除处理和 no-store/no-referrer 策略，但仍需检查屏幕内容及接收方。**Feedback…** 仅打开发给 `z5866318@gmail.com` 的邮件草稿，不自动发送或附加截图；你决定内容和是否发送。安全问题见 [SECURITY](SECURITY.md)。
