// dear imgui: standalone example application for SDL2 + OpenGL
// If you are new to dear imgui, see examples/README.txt and documentation at the top of imgui.cpp.
// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan graphics context creation, etc.)
// (GL3W is a helper library to access OpenGL functions since there is no standard header to access modern OpenGL functions easily. Alternatives are GLEW, Glad, etc.)

#include <string>
#include <sstream>
#include <assert.h>
#include <iostream>
#include <set>
#include <thread>

#include "../imgui/imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <SDL2/SDL.h>
#include "Player.hpp"

#ifdef main
#undef main
#endif
// About OpenGL function loaders: modern OpenGL doesn't have a standard header file and requires individual function pointers to be loaded manually.
// Helper libraries are often used for this purpose! Here we are supporting a few common ones: gl3w, glew, glad.
// You may use another loader/header of your choice (glext, glLoadGen, etc.), or chose to manually implement your own.
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
#include <GL/gl3w.h>    // Initialize with gl3wInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
#include <GL/glew.h>    // Initialize with glewInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#include <glad/glad.h>  // Initialize with gladLoadGL()
#else
#include IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

struct image_buffer
{
	unsigned char* pixels;
	size_t upload_size;
	int width;
	int height;

	GLuint g_FontTexture = 0;
};

image_buffer the_video;


ImTextureID create_image_buffer(image_buffer& the_image)
{
	// Build texture atlas
	//unsigned char* pixels;
	//int width, height;
	//io.Fonts->GetTexDataAsRGBA32(&the_image.pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.
	the_image.pixels = (unsigned char*)malloc(the_image.width * the_image.height * 4);

	for (int i = 0; i < the_image.width * the_image.height * 4; i += 4)
	{
		*(the_image.pixels + i + 0) = 255;
		*(the_image.pixels + i + 1) = 0;
		*(the_image.pixels + i + 2) = 0;
		*(the_image.pixels + i + 3) = 255;
	}/**/

	//io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	//io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	the_image.upload_size = the_image.width * the_image.height * 4 * sizeof(char);
	// Upload texture to graphics system
	GLint last_texture;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
	glGenTextures(1, &the_image.g_FontTexture);
	glBindTexture(GL_TEXTURE_2D, the_image.g_FontTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#ifdef GL_UNPACK_ROW_LENGTH
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, the_image.width, the_image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, the_image.pixels);

	// Store our identifier
	//io.Fonts->TexID = (ImTextureID)(intptr_t)the_image.g_FontTexture;

	// Restore state
	glBindTexture(GL_TEXTURE_2D, last_texture);

	return (ImTextureID)(intptr_t)the_image.g_FontTexture;
}


bool check_floating_point(std::string astring)
{
	std::istringstream iss(astring);
	float tmp;
	iss >> std::noskipws >> tmp;
	return iss.eof() && !iss.fail();
}

filter_D filter;
std::vector<uint64_t> diffs;

bool done = false;

int main(int argc, const char *argv[])
{
	if (argc != 7) {
		std::cout << "Usage: ./player filename width_start width_end height_start height_end video_output[0,1]" << std::endl;
		exit(-1);
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		std::cout << "There is something wrong with your SDL Libs. Can't run" << std::endl;
		exit(-1);
	}


	//"20180823_134013.MOV"
	std::string filename = std::string(argv[1]);

	bool check_sw = check_floating_point(argv[2]);
	bool check_ew = check_floating_point(argv[3]);
	bool check_sh = check_floating_point(argv[4]);
	bool check_eh = check_floating_point(argv[5]);

	bool video_output = std::stoi(argv[6]);

	if (!check_sw || !check_ew || !check_sh || !check_eh)
	{
		std::cout << "Wrong: Windows dimentions must be between [0..1]" << std::endl;
		std::cout << "Usage: ./player filename width_start width_end height_start height_end video_output" << std::endl;
		exit(-1);
	}

	float start_w_param = std::atof(argv[2]);
	float end_w_param = std::atof(argv[3]);
	float start_h_param = std::atof(argv[4]);
	float end_h_param = std::atof(argv[5]);

	//Player * player = new Player(filename, start_w_param, end_w_param, start_h_param, end_h_param, video_output);

	filter.finfo.start_w = start_w_param;
	filter.finfo.end_w = end_w_param;
	filter.finfo.start_h = start_h_param;
	filter.finfo.end_h = end_h_param;

	filter.info_file = { filename };
	init_ffmpeg(filter.info_file, filter.info_ffmpeg, filter.wanted_spec, filter.audio_spec, filter.audio_p, filter.wanted_frame);
	init_frame(filter.frame);

	for (int i = 0; i < 1920; i++)
	{
		for (int j = 0; j < 1080; j++)
		{
			filter.finfo.variance[0][i][j] = 0;
			filter.finfo.variance[1][i][j] = 0;
		}
	}


    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Decide GL+GLSL versions
#if __APPLE__
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
    bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
    bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
    bool err = gladLoadGL() == 0;
#else
    bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader is likely to requires some form of initialization.
#endif
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer bindings
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	uint64_t frame_number = 0;

	std::thread filter_worker;
	filter_worker = std::thread([&]() {
		int res = 0;
		while (res = ffmpeg_step(filter.info_ffmpeg, filter.frame, filter.packet, filter.audio_p), res != 1 && !done)
		{

			if (res != -2) {

				int numBytes = av_image_get_buffer_size(FORMATO, filter.info_ffmpeg.pCodecCtx->width, filter.info_ffmpeg.pCodecCtx->height, 1);
				filter.finfo.data = filter.frame.pFrame->data[0];
				filter.finfo.b_width = filter.info_ffmpeg.pCodecCtx->width;
				filter.finfo.b_height = filter.info_ffmpeg.pCodecCtx->height;
				filter.finfo.b_offset = 3;

				filter_step(filter.finfo, diffs);

				if (!diffs.empty() && diffs[diffs.size() - 1] > 300) filter.frame.flying_frame_counter++;
			}
		}
	});/**/

	ImTextureID img_id;

	the_video.width = filter.info_ffmpeg.pCodecCtx->width;
	the_video.height = filter.info_ffmpeg.pCodecCtx->height;
	img_id = create_image_buffer(the_video);

    // Main loop
    
    while (!done)
    {
		int res = 2;
		//int res = player->lerFramesVideo(frame_number++);


		for (int i = 0; i < filter.finfo.b_height * filter.finfo.b_width && res == 2; i += 1)
		{
			*(the_video.pixels + i * 4 + 0) = filter.finfo.data[i];
			*(the_video.pixels + i * 4 + 1) = filter.finfo.data[i];
			*(the_video.pixels + i * 4 + 2) = filter.finfo.data[i];
			*(the_video.pixels + i * 4 + 3) = filter.finfo.data[i];
		}/**/


		// Upload to Buffer:
		{
			glBindTexture(GL_TEXTURE_2D, the_video.g_FontTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#ifdef GL_UNPACK_ROW_LENGTH
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, the_video.width, the_video.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, the_video.pixels);
		}
/**/


        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

		ImGui::Begin("Video");   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
		ImGui::Image(img_id, ImVec2(1920 * 0.4, 1080 * 0.4), ImVec2(0, 0), ImVec2(1, 1), ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 128));
		ImGui::End();


		ImGui::Begin("Video Information");                         
		ImGui::Text("Current flying time: "); ImGui::Text(std::to_string(filter.frame.flying_frame_counter * filter.frame.frame_in_seconds).c_str());

		if (ImGui::Button("Stop"))
		{
			done = true;
		}
		ImGui::End();
        
		
		
		// Rendering
        ImGui::Render();
        SDL_GL_MakeCurrent(window, gl_context);
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

	
	filter_worker.join();

	std::ofstream fd_output_file("f_d_" + filter.info_file.filename + ".txt");
	std::ofstream time_output_file("f_t_" + filter.info_file.filename + ".txt");
	for (int i = 0; i < diffs.size(); ++i)
	{
		fd_output_file << diffs[i] << std::endl;
		//if (diffs[i] > 999999999) filter.frame.flying_frame_counter++;
	}

	float flying_time_in_seconds = (float)filter.frame.flying_frame_counter * (float)filter.frame.frame_in_seconds;
	time_output_file << flying_time_in_seconds << std::endl;
	std::cout << flying_time_in_seconds << std::endl;
	fd_output_file.close();
	time_output_file.close();
	//return 1;/**/


    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);


	av_free(filter.frame.pFrame);
	avcodec_close(filter.info_ffmpeg.pCodecCtx);
	av_free(filter.info_ffmpeg.pCodecCtx);
	avformat_close_input(&filter.info_ffmpeg.pFormatCtx);
	//SDL_CloseAudio();
	SDL_Quit();

    return 0;
}
