#include "Player.hpp"
#include <string>
#include <sstream>

#ifdef main
#undef main
#endif

using namespace std;


bool check_floating_point(std::string astring)
{
	std::istringstream iss(astring);
	float tmp;
	iss >> noskipws >> tmp;
	return iss.eof() && !iss.fail();
}

int main(int argc, const char *argv[]) {

	if(argc != 7){
		cout << "Usage: ./player filename width_start width_end height_start height_end video_output[0,1]" << endl;
		exit(-1);
	}
	
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		cout << "There is something wrong with your SDL Libs. Can't run: " << SDL_GetError() << endl;
		exit(-1);
	}

	//"20180823_134013.MOV"
	std::string filename = std::string(argv[1]);
	
	bool check_sw = check_floating_point(argv[2]);
	bool check_ew = check_floating_point(argv[3]);
	bool check_sh = check_floating_point(argv[4]);
	bool check_eh = check_floating_point(argv[5]);

	bool video_output = stoi(argv[6]);

	if (!check_sw || !check_ew || !check_sh || !check_eh)
	{
		cout << "Wrong: Windows dimentions must be between [0..1]" << endl;
		cout << "Usage: ./player filename width_start width_end height_start height_end video_output" << endl;
		exit(-1);
	}
	
	float start_w_param = std::atof(argv[2]);
	float end_w_param = std::atof(argv[3]);
	float start_h_param = std::atof(argv[4]);
	float end_h_param = std::atof(argv[5]);

	
	Player * player = new Player(filename, start_w_param, end_w_param, start_h_param, end_h_param, video_output);
	
	int res = player->alloc_memory();
	if (res < 0) {
		cout << "Fatal Error";
		delete(player);
		exit(-1);
	}

	res= player->create_window();

	res = player->lerFramesVideo();
	if (res < 0) {
		cout << "Shit happens, try again" << endl;
		delete(player);
		exit(-1);
	}

	delete(player);

	return 0;
}
