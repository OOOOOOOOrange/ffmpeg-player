# ffmpeg-player
根据雷霄骅博士的博客https://blog.csdn.net/leixiaohua1020/article/details/8652605

学习了每一条语句背后的含义并添加了注释

运用了ffmpeg解码输入视频流

利用SDL显示解码的视频

本项目运行于macOS平台

需使用homebrew下载好ffmpeg及SDL

若下载路径非默认需在Xcode的中修改searchpath到对应的include文件夹

使用方法 ./mp4player+视频文件路径

如 ./mp4player /Users/mac/Desktop/2.mp4
