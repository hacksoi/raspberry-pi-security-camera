sudo ffmpeg -y -f v4l2 -i /dev/video0 -vcodec mjpeg -f mjpeg -pix_fmt rgba -an - | ./zec_cam
