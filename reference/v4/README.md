# 合拍 / Echoy · 方案 B v4 — 合并式控制条

打开 `echoy-arrange-B-v4.html`。文件内嵌声音与界面，无需安装依赖。它是在用户提供的 `echoy-arrange-B-v3.html` 上增量修改的交互原型，没有覆盖 v3 或冻结的 v5.1.0。

## 本次采用的已选方案

- 首页底部：角色 / 合并式控制条 / 设置。
- 空闲：控制条内并排“录制｜播放”。没有可播放内容时，播放不可用；跟随左邻时，本地录制保持不可用。
- 预备、录入：控制条整条变为“取消”，原来的两个分段不接受输入。
- 单次试听、持续演奏及其准备：整条变为“暂停”。录制入口不再同时出现。
- 约240ms平滑展开/合并，过渡期间取消/暂停立即响应。不需要等动画完成。系统开启减少动态效果时，不执行形变动画。
- 角色与表达改成左上单行“声线 · 短句”，电量仍在右上。两侧滑轨和中央声形上移，保持300px声形与16格反馈同宽。
- 删除“开头可以留空 · 提示音不会录入”常驻文字。正常演奏没有重复操作说明；预备拍数、录入进度、等待生效、单次试听和必要消息仍有简短提示。

## 原玩法不变

中央普通敲击只是试奏。点录制后先预备两拍，再自动录四拍，允许第一拍留空。空录或取消保留原节奏。完成后只试听一次，点播放才进入稳定重复；暂停只影响本机。

拖外壳吸合，确认连接后，各台独立决定是否发声。十二种附和、角色与表达页、共享拍点、向右影响、换位、拆开保留和异常处理均沿用 v3。

## 测试与复现

包内 `source/` 保留输入快照、增量源文件及验证证据。构建和测试脚本需要容器已有的 Python Playwright、Chromium、Node；普通使用HTML不需要它们。

```sh
cd source
python apply_patch.py
python build.py
node --test tests/model.test.cjs tests/resume_clock.test.cjs
python tests/merged_contract.py index.html final
python tests/layout_v4.py
python tests/multi_v4.py
python tests/audio_v4.py
```

`apply_patch.py` 总是从基线副本打补丁，不累计重复改动；`build.py` 生成源码目录的 `index.html` 和其上一级的 `echoy-arrange-B-v4.html`。截图脚本只用于开发验收，不是产品依赖。

细节和未验证范围见 `verification.md`。这不是固件实现或真机验收。
