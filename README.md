## drosophila
drosophila is a tiny program that evaluates videos and calculates the amount of time the drosophila was flying. We are using a 5x5 pixel neigbourhood to calculate the relevant difference between consecutive frames of the video within a parameter size mask of relevance.

#g++ main.cpp Player.cpp -std=gnu++0x -lSDL2 -lavdevice -lavformat -lavfilter -lavcodec -lswresample -lswscale -lavutil -lz -lm -lpthread -lswresample


example:

./a.out "20180823_134013.MOV" 0.08 0.7 0.25 0.8 0

where:
* "20180823_134013.MOV" video file name
* 0.08 -> window width start 
* 0.7 -> window width end
* 0.25 -> window height start
* 0.8 -> window height end
* 0 indicates we do not want to playback the video in real time (value could be switched to 1 if you wanted live view)



EmiHoss - Galimba 2019
