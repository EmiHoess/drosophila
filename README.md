# drosophila

g++ main.cpp Player.cpp -std=gnu++0x -lSDL2 -lavdevice -lavformat -lavfilter -lavcodec -lswresample -lswscale -lavutil -lz -lm -lpthread -lswresample


ejemplo:

./programa "20180823_134013.MOV" 0.08 0.7 0.25 0.8 0

donde:
* "20180823_134013.MOV" es el nombre del archivo
* 0.08 es el ancho donde arranca la ventana
* 0.7 es el ancho donde finaliza la ventana
* 0.25 es el alto donde arranca la ventana
* 0.8 es el alto donde finaliza la ventana
* 0 indica que no se quiere abrir el player. (Para abrirlo, usar 1)
