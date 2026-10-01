# 恢复、更新与卸载

展开 SeeThis 菜单，核对 **App:** 和 **Location:** 的实际运行路径。保存版本和附件 SHA-256，避免把不同副本的状态混入反馈。

| 状态或现象 | 下一步 |
| --- | --- |
| Input Monitoring not ready/denied | 在系统 Input Monitoring 允许确切 app，点 Retry Input Monitoring；restart_required 时手动退出重开 |
| Screen Recording not ready | 打开 Screen Recording Settings，允许确切 app，按系统提示重开，再真实圈选 |
| Quit & Reopen 后仍不 ready | 退出重复副本，从已授权路径手动打开，核对 Location 和各项 readiness |
| 权限 ready 但快捷键失效 | 核对 Inspector/设置显示的实际 Capture 和 Delete 配置；松开所有键，Retry Input Monitoring |
| Link copied 但没有图片 | 等到 ready，再显式点 Copy marked image，后台完成不更改剪贴板 |
| 远程聊天打不开 URL | 粘贴/附加实际图片，同机 URL 不向远程公开 |
| Chrome 切 tab 后 mark 隐藏 | 回到原 app/window/tab/page，等待页面观测，核对 Automation |
| Chrome 无条目或被拒绝 | 真正激活独立 Chrome 后，在恢复区选 Connect Chrome；设置允许后 Retry Chrome，见 [CHROME](CHROME.md) |
| View details/复制不可用 | 检查 pending/failed/expired/deleted，等待或重新采集安全内容 |
| Finder 拷贝失败 | 保存确切错误，重新核对摘要，可用同 Release 的 ZIP 替代方式；当前 DMG 拖放未完整复测 |
| damaged/恶意软件/身份异常 | 暂停运行，核对官方附件摘要和来源，提交安全复现；不要关闭系统保护 |
| 公司设备无法授权/安装 | 联系设备管理员，遵守设备策略 |

## 更新

下载新附件和 SHA256SUMS，按[安装步骤](INSTALL.md)核对。阅读该版本已知限制；在旧 app 菜单选 **Quit SeeThis**，退出其他副本，再把已校验的新 app 放入原 Applications 位置。出现替换提示时核对目标，保留旧包直到新版本正常。

手动打开确切路径，核对 Location 和 Input Monitoring、Screen Recording、Capture/Delete shortcut。必要时允许、Retry 或退出重开。普通替换保留 Application Support 设置和有效引用；过期/删除引用不恢复。签名改变、多副本和系统政策可能影响 TCC，不能保证现场权限连续性。SeeThis 没有自动更新；完整更新/回滚矩阵尚未复测。

## 卸载与保留数据

1. 选 **Quit SeeThis**，在 Finder 将计划卸载的 Applications/SeeThis.app 移到 Trash。卸载程序保留用户数据和系统授权条目。
2. 要保留设置/引用，就保留 `~/Library/Application Support/SeeThis`。
3. 确定不要本机数据时，在 Finder **Go > Go to Folder…** 输入上述路径，核对后自行移到 Trash。`settings-v1.json` 是设置，`references-v1` 是引用与截图；不要删其他 Application Support 内容。
4. 系统权限可在 Privacy & Security 关闭或移除 SeeThis，无需 TCC reset。清空 Trash 是单独不可逆动作，也不会清除备份、剪贴板或已分享附件。

这些步骤说明数据机制，完整卸载/残留实测仍未完成。

## 反馈

记录版本、附件 SHA-256、macOS/Chrome 版本、准确状态、步骤和预期/实际结果。先在无私人内容的测试页重现。普通问题可到 [GitHub Issues](https://github.com/amingclawdev/seethis/issues/new/choose)，或用 Inspector **Feedback…** 打开 `z5866318@gmail.com` 的邮件草稿。你决定是否发送，app 不自动附加资料。

只分享必要的安全截图，不公开全桌面、真实 tab 地址/标题、引用 capability 或设置访问地址。Console 中 `seethis capture` / `seethis chrome` 状态/原因可以逐条检查后分享，不上传整份日志。安全问题先用 [SECURITY](../SECURITY.md) 的私下通道。
