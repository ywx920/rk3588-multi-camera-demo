# GitHub备份操作

本仓库保存ZX2603-2 RK3588摄像头、Qt、Web、RKNN源码、配置、模型和调试文档。
Python虚拟环境、下载缓存、构建目录、RKISP Tuner资料和大型压缩包由`.gitignore`
排除，应另行保存到NAS或移动硬盘。

## 首次关联GitHub

先在GitHub创建一个空的Private仓库，不初始化README或License，然后执行：

```bash
cd /home/wx/2-demoRK
git config user.name "你的GitHub用户名"
git config user.email "你的GitHub邮箱"
git remote add origin git@github.com:你的用户名/仓库名.git
git push -u origin main
```

若使用HTTPS：

```bash
git remote add origin https://github.com/你的用户名/仓库名.git
git push -u origin main
```

## 日常备份

```bash
cd /home/wx/2-demoRK
git status
git add -A
git diff --cached --stat
git commit -m "说明本次修改"
git push
```

## 检查未备份内容

```bash
git status --ignored --short
git check-ignore -v 路径
```

被忽略的大型资料不会上传GitHub，需要单独备份：

```text
ov13850.rar
ov13850_materials/
j7-yolov5-rknn-pc/.venv/
j7-yolov5-rknn-pc/.python/
j7-yolov5-rknn-pc/downloads/
j7-yolov5-rknn-pc/third_party/
```
