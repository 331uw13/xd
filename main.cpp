#include <sys/types.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <libgen.h>
#include <pwd.h>
#include <termios.h>
#include <dirent.h>
#include <unordered_map>
#include <iostream>
#include <vector>

//  TODO ...
//
//  [] - update draw
//  [] - create config file for settings and colors
//  [] - error handling
//
//

#define EXIT     101     // 'e'
#define UP       115     // 's'        |  move index up
#define DOWN     100     // 'd'        |  move index down
#define BACK     97      // 'a'        |  same as "cd .."
#define PREVIOUS 102     // 'f'        |  move to previous directory
#define CONFIRM  10		 // enter      |  open file or change directory

std::unordered_map<std::string, uint32_t> colormap;

#define COLOR__SIZE      "\033[38;5;30m"
#define COLOR__OWNER     "\033[38;5;71m"

#define COLOR__DIR       "\033[34m"
#define COLOR__FILE      ""
#define COLOR__EXEC      "\033[32m"
#define COLOR__LINK      "\033[36m"
#define COLOR__SELECTED  "\033[4m"

void _add_colors() {

	colormap[".o"]    = 94;
	colormap[".cpp"]  = 99;
	colormap[".hpp"]  = 99;
	colormap[".h"]    = 99;
	colormap[".c"]    = 99;
	colormap[".hh"]   = 99;
	colormap[".cc"]   = 99;
	colormap[".asm"]  = 131;
	colormap[".wav"]  = 1;
	colormap[".mp3"]  = 1;

}

struct _global {
	
	uint32_t line_count;
	uint32_t position;
	uint32_t selected;

	const char* dir;
	const char* previous_dir;
	
	bool open;

} global;

struct _settings {
	
	uint32_t max_height;

	const char* texteditor;	
	
	bool show_hidden;	
	bool more_colors;
	bool directories_first;

} settings;

struct file_data {
	
	dirent d;
	off_t size;
	char* owner;

	std::string color;
};

std::vector<file_data> files; // everything in current directory

// ###############################

char getch() {	
    char buf = 0;
    struct termios old;
    fflush(stdout);
    
	if(tcgetattr(0, &old) < 0)
        perror("tcsetattr()");
    
	old.c_lflag &= ~ICANON;
    old.c_lflag &= ~ECHO;
    old.c_cc[VMIN] = 1;
    old.c_cc[VTIME] = 0;
    
	if(tcsetattr(0, TCSANOW, &old) < 0)
        perror("tcsetattr ICANON");
    
	if(read(0, &buf, 1) < 0)
        perror("read()");
    
	old.c_lflag |= ICANON;
    old.c_lflag |= ECHO;
    
	if(tcsetattr(0, TCSADRAIN, &old) < 0)
        perror("tcsetattr ~ICANON");
	
	return buf;
}

void swap(int a, int b) {
	file_data tmp = files[a];
	files[a] = files[b];
	files[b] = tmp;
}

void sort_files() {
	int a;
	int max = files.size();
	
	if(max < 2)
	   return;

	if(settings.directories_first) {
		a = 0;
		for(int i = 0; i < max; i++) {
			if(files[i].d.d_type == DT_DIR) {
				swap(a, i);
				if(a < max - 1) a++;
			}
		}
	} else {
		a = max - 1;
		for(int i = max; i > 0; i--) {
			if(files[i].d.d_type == DT_DIR) {
				swap(i, a);
				if(a > 0) a--;
			}
		}
	}
}

void get_files(const char* dir_name) {
	if(dir_name == "") {
		dir_name = get_current_dir_name();
	}

	// test if it exists
	if(access(dir_name, R_OK) != 0)
		return;

	// retrieve information
	struct stat s;
	if(stat(dir_name, &s) < 0)
		return;

	// check if we are working with directory here
	if(!S_ISDIR(s.st_mode))
		return;

	// get information about file and save it
	struct dirent *_dir;
  	DIR* d = opendir(dir_name);
  	if(d) {
		files.clear();
		global.line_count = 0;

		struct passwd* pw;
		while ((_dir = readdir(d)) != NULL) {
			if(_dir->d_name[0] != '.' || settings.show_hidden) {
			
				if(stat(_dir->d_name, &s) < 0)
					continue;

				if(global.line_count < settings.max_height)
					global.line_count++;
				
				file_data data;
				data.d = *_dir;
				data.size = s.st_size;
				if((pw = getpwuid(s.st_uid)) != NULL)
					data.owner = pw->pw_name;

				if(data.d.d_type == DT_REG) {
					int exec_ok = access(data.d.d_name, X_OK);
					if(exec_ok != 0) {
						std::string name = data.d.d_name;
						int f = name.find_last_of('.');
						if(f != std::string::npos) {
							std::string ext = name.substr(f, name.length());
							std::unordered_map<std::string, uint32_t>::const_iterator f = colormap.find(ext);
							if(f != colormap.end())
								data.color = "\033[38;5;" + std::to_string(f->second) + "m";
							else
								data.color = COLOR__FILE;
						}
					}
					else if(exec_ok == 0)
						data.color = COLOR__EXEC;
					else 
						data.color = COLOR__FILE;
				}
				else if(data.d.d_type == DT_DIR)
					data.color = COLOR__DIR;
				else if(data.d.d_type == DT_LNK)
					data.color = COLOR__LINK;

				files.push_back(data);
			}
		}
    	closedir(d);
		sort_files();
  	}	
}

void clear_lines(int count) {
	for(int i = 0; i < count; i++) 
		printf("\033[K\n");

	printf("\033[%iA", count); // move up
}

void move_dir(const char* name) {
	global.previous_dir = get_current_dir_name();
	uint32_t previous_line_count = global.line_count + 1;

	chdir(name);
	global.dir = get_current_dir_name();
	get_files(global.dir);	
	
	if(previous_line_count > global.line_count)
		clear_lines(previous_line_count);
	
	global.position = 0;
	global.selected = 0;
}

void enter_key_press() {
	const char* n = files[global.selected].d.d_name;

	if(files[global.selected].d.d_type == DT_DIR) {
		move_dir(n);

	} else {
		if(settings.texteditor == nullptr || settings.texteditor == "")
			return;
		
		// TODO: check link

		std::string cmd = settings.texteditor;
		cmd += " ";
		cmd += n;
		system(cmd.c_str());

		global.open = false;
	}
}

void input_handler() {
	int i = getch();
	switch(i) {
		case EXIT:
			global.open = false;
			break;
		
		case UP:
			if(global.selected > 0)
				global.selected--;
			
			if(global.selected < global.position) {
				if(global.position > 0)
					global.position--;
			}
			break;
	
		case DOWN:
			if(global.selected < files.size() - 1)
				global.selected++;

			if(global.selected > settings.max_height + global.position - 1) {
				if(global.position < files.size() - settings.max_height)
					global.position++;
			}
			break;

		case BACK:
			move_dir("..");
			break;

		case PREVIOUS:
			move_dir(global.previous_dir);
			global.previous_dir = global.dir;
			break;

		case CONFIRM:
			if(files.size() >= global.selected)
				enter_key_press();
			break;

		default: break;
	}
}

void draw() {
	
	printf("\033[K\033[48;5;233m\033[90m[\033[31m    %i/%i  %s    \033[90m]\033[0m\n", 
			global.position, files.size() - global.line_count, global.dir);
	
	if(files.size() > 0) {
		for(int i = global.position; i < global.line_count + global.position; i++) {
			printf("\033[K");
			if(global.selected == i) {
				printf("\033[48;5;233m%s>%s %s \033[0m\033[48;5;233m \033[90m<< %s%i \033[90m| %s%s\033[0m\n",
						files[i].color.c_str(), COLOR__SELECTED, files[i].d.d_name,

						COLOR__SIZE, files[i].size,
						COLOR__OWNER, files[i].owner
					
					);
			}
			else {
				printf(" %s %s \033[0m\n", files[i].color.c_str(), files[i].d.d_name);
			}
		}
	}

	// move cursor up
	printf("\033[K\033[%iA", global.line_count + 1);
}

int main(int argc, char** argv) {
	global.open = true;

	// TODO: load settings from config file
	settings.texteditor = "/usr/bin/vim"; // open file with this
	settings.show_hidden = false; // show files that starts with '.'?
	settings.more_colors = true; // color filenames by their filename extension?
	settings.directories_first = true; // print directories first then files?
	settings.max_height = 20; // how many items are visible at the time?
	// -------------
	
	if(settings.more_colors)
		_add_colors();

	// TODO: add argument handling!
	if(argc == 2)
		global.dir = argv[1];
	else
		global.dir = get_current_dir_name();
	// ------------
		
	global.previous_dir = global.dir;
	get_files(global.dir);

	while(global.open) {

		draw();
		input_handler();
	
	}

	printf("\033[%iB", global.line_count + 1);
	return 0;
}


