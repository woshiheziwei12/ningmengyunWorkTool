# 仓库规则

- 维护独立的[全局规则活文档](QtWidgetsApplication1/docs/global-rules.md)：开始修改前阅读相关规则；新增、调整或发现跨功能行为与文档不一致时，在同一变更中同步实现和文档，保持规则、适用范围及验证方式与当前行为一致。
- 新增或修改桌面业务功能时，同一变更同步更新 CLI 入口和[命令 API 手册](QtWidgetsApplication1/docs/command-api.md)。GUI 与 CLI 复用业务服务，并验证命令、参数、示例和实际结果。
