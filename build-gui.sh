cd /home/pi/app
gcc gui.c -o cw-gui -I/usr/include/postgresql -L/usr/lib/arm-linux/gnueabihf -lpq -lncurses -lm -ldl
cd -
/home/pi/app/cw-gui
