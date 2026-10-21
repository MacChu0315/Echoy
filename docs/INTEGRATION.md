# 技能分支分发与工程接入

## 当前分发位置

用户明确选择更新现有 `MacChu0315/Echoy` 的 `skills/mosaico-development` 分支，把其中内容替换为新的 `mosaico-dev-skills`。不另建远程仓库、不新增远端发布分支、不改动 `master`。在旧技能分支上顺接提交，保留其技能历史，不合并产品或独立库的 Git 历史。

本次内容来源是本地独立库 `8958188444ef5ab0050c3d2901db794097ef2eef`。9项技能正文及参考保持一致，更新根入口、状态与接入文档，使其对应现有技能分支，而不是先前的独立建仓计划。

## 直接使用

```sh
git clone --single-branch --branch skills/mosaico-development https://github.com/MacChu0315/Echoy.git mosaico-dev-skills
cd mosaico-dev-skills
python3 -m unittest discover -s tests -v
```

按实际任务读取 `.agents/skills/` 下6项工程技能，或者手动选用 `skills/` 下3项试用技能。旧试用元数据和未执行评估保持不变。不要将整套技能都强加给简单任务或更改全局配置。

## 工程作为 submodule 消费

新消费工程在适用授权内，可以使用现有仓库的技能分支：

```sh
git submodule add -b skills/mosaico-development --depth 1 https://github.com/MacChu0315/Echoy.git .agents/mosaico-dev-skills
```

`-b` 表示选用的分支；父项目仍通过 gitlink 固定实际 commit，并不自动跟随远端更新。读取该固定 commit 的变更、验证后，才更新父项目指针。若工程已经存在该路径或有同名定制技能，先核对配置、未提交修改和现有内容，不能直接覆盖或再次 add。

技能目录可链接到库中的整个目录，避免重复维护：

```text
project/
  .agents/
    mosaico-dev-skills/        # submodule：Echoy.git 的技能分支
    skills/
      mosaico-gsp-parity -> ../mosaico-dev-skills/.agents/skills/mosaico-gsp-parity
      ...                     # 其余工程技能按需链接
```

保留每项技能内的 `references/`。客户端是否自动发现 symlink 需单独核实；也可以直接提供 `SKILL.md` 路径。3项试用技能不自动加入工程发现目录。技能库分支本身没有 submodule 或产品源码；消费同一个远程仓库的技能分支不形成递归产品依赖。

## 先前本地接入的定位

先前独立本地库和 Echoy 的 `chore/shared-dev-skills` 是已保留的本地成果，不是本次要发布的分支。它们的历史克隆检查仍有效，但不证明当前远程接入配置。

旧接入中 `../mosaico-dev-skills` 及独立库 commit8958188 指向另一个本地仓库；对应的同名远程仓库未建立。若后续迁移该消费工程，应把模块 URL／branch 改为本节的现有 Echoy 远程／技能分支，并将 gitlink 更新到**实际发布到该分支的 commit**。文件相同不代表不同仓库的 commit 可以互换。

本次只更新技能分支内容。原本地接入分支保留，不把归档应用历史合入产品 `master`，也不顺手改动其他 worktree。

## 发布验证与边界

发布前核实目标为既有技能分支、旧提交为当前远端祖先、差异和可达历史只包含技能材料。使用普通 fast-forward push，禁用附带推送 tag；不强推、不删除分支、不创建 PR，不修改默认分支。

发布后只读核对远端分支 SHA，再从远端单分支克隆、运行库测试并检查目录／参考。构建、设备、客户端自动发现和产品运行不在这次文档分发的验证范围。远程写入若结果不明确，先只读核实，不直接重复提交写操作。
