# Changelog

本项目遵循 [Semantic Versioning](https://semver.org/)。

## [0.1.0] - 2026-08-13

首个公开预发布版本。

### Added

- Pipeline V2：按原生 HUD、消息和渲染事务建立有边界的 POV 身份作用域。
- 圆形雷达、队友颜色/头顶信息、顶部队伍信息和实战底部 HUD。
- 原生完整闪光、语音状态、购买提示、聊天与无线电消息。
- 原生受击方向弧、死亡后处理和击杀横幅。
- Demo 缺失 live 输入时的击杀奖励、投掷物播报与雷达声音事件补偿。
- seek 与 POV 切换后的雷达和身份恢复。
- `engine2.dll`、`client.dll` 双 PE 指纹与关键字节安全门禁。
- 原生 Windows 启动器 GUI：选择 Demo 启动，或只启动 CS2 后手动 `playdemo`。
- GUI 固定参数/控制台指令面板和命令行兼容入口。
- Release ZIP、SHA-256 校验文件、CMake Presets 和 GitHub Actions。
- 18 项 Release 回归测试。

### Changed

- 将 detour、POV 上下文、事件补偿和启动命令拆分为独立模块。
- 正式启动器自动设置 `LIVE_HUD_PIPELINE=1`，无需用户手动配置。

### Known issues

- 部分第三方 Demo 中普通奔跑脚步的雷达声音圈仍不能稳定出现；起跳已覆盖，落地优先依赖原生声音输入。
- 固定 RVA 仅适配 README 列出的 2026-08-13 客户端构建。
