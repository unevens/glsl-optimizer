#include "../glsl/glsl_optimizer.h"
#include "dirent.h"
#include <string>
#include <vector>
#include <fstream>
#include <string>
#include <cerrno>
#include <utility>
#include <iostream>
#include <sstream>

using namespace std;

void usage() {
#ifdef _WIN32
	cout << "Usage:"<< endl << "$ oglsl.exe [ -h | -m ] paths_to_shader_files_or_folders_containing_shaders" << endl;
#else
	cout << "Usage:" << endl << "$ oglsl [ -h | -m ] paths_to_shader_files_or_folders_containing_shaders" << endl;
#endif
	cout << "Supported extensions are '.frag'  and '.vert' for full shaders, and '.glsl' for headers." << endl;
	cout << "Use -h to force all the precision qualifiers in the fragments shaders to be 'highp'." << endl;
	cout << "Use -m to force all the precision qualifiers in the fragments shaders to be 'mediump'." << endl;
}

bool force_high_precision = false;
bool force_medium_precision = false;

struct shader_input {
	string path;
	string source;
	string comment;
	glslopt_shader_type type;
	bool complete;
	shader_input(string& path, string& source, string& comment, glslopt_shader_type type, bool complete) :
		path(path), source(source), type(type), complete(complete), comment(comment)
	{}
};

string read_file(const char *filename){
	ifstream in(filename, ios::in | ios::binary);
	if (in){
		string contents;
		in.seekg(0, ios::end);
		contents.resize(in.tellg());
		in.seekg(0, ios::beg);
		in.read(&contents[0], contents.size());
		in.close();
		return(contents);
	}
	throw(errno);
}

void replace_string(string& subject, const string& search, const std::string& replace) {
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != string::npos) {
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
}

void save_shader(glslopt_shader* shader, shader_input& shaderInput) {
	string opt_source = string(glslopt_get_output(shader));
	auto& path = shaderInput.path;
	string ext = path.substr(path.length() - 5);
	string name = path.substr(0, path.length() - 5);
	string save_path = name;
	save_path += ".opt" + ext;
	auto file = ofstream(save_path);
	file << shaderInput.comment << "\n"<< opt_source;
	file.close();
	cout << "Saved optimized shader: " << save_path << endl;
}

void process_shader(glslopt_ctx* ctx, shader_input& shader_data) {
	auto shader = glslopt_optimize(
		ctx,
		shader_data.type,
		shader_data.source.c_str(),
		shader_data.complete ? 0 : kGlslOptionNotFullShader
	);
	if (glslopt_get_status(shader)) {
		save_shader(shader, shader_data);
	}
	else {
		stringstream s(shader_data.source);
		string line;
		int i = 1;
		while (getline(s, line, '\n')) {
			cerr << i++ << " " << line << endl;
		}
		cerr << glslopt_get_log(shader);
	}
	glslopt_shader_delete(shader);
}

void process_shaders(vector<shader_input>& shaders_data, glslopt_target target) {
	auto ctx = glslopt_initialize(target);
	int num_shaders = shaders_data.size();
	for (int i = 0; i < shaders_data.size(); ++i) {
		process_shader(ctx, shaders_data[i]);
		cout << i + 1 << " of " << num_shaders << endl;
	}
	glslopt_cleanup(ctx);
}

int is_dot_or_dot_dot(char const* name)
{
	return (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
}

int main(int argc, char **argv) {
	
	cout << "oglsl: batch glsl optimizer https://github.com/unevens/oglsl based on https://github.com/aras-p/glsl-optimizer" << endl;

#ifdef _DEBUG

	cout << "This is a debug build. Press enter to continue." << endl;
	cin.get();

#endif

	if (argc < 2) {
		usage();
		return 1;
	}

	if (argv[1][0] == '?') {
		usage();
		return 1;
	}

	vector<shader_input> shaders_gles2;
	vector<shader_input> shaders_gles3;

	int i =  1;
	
	if (argv[1][0] == '-') {
		string options = argv[1];
		force_high_precision = options.compare("-h") == 0;
		force_medium_precision = options.compare("-m") == 0;
		if(!force_high_precision && !force_medium_precision) {
			usage();
			return 1;
		}
		i = 2;
	}

	vector<string> filepaths;
	vector<string> inputs;
	for (; i < argc; ++i) {
		inputs.push_back(argv[i]);
	}
	DIR *dir;
	dirent *ent;
	while (inputs.size() > 0) {
		string input = inputs.back();
		inputs.pop_back();

		if ((dir = opendir(input.c_str())) != NULL) {
			cout << "Gathering shaders in directory: " << input << endl;
			while ((ent = readdir(dir)) != NULL) {
				if (is_dot_or_dot_dot(ent->d_name)) continue;
				if (ent->d_type == DT_DIR) {
					inputs.push_back(input + "/" + ent->d_name);
					cout << "Found subdirectory: " << ent->d_name << endl;
				}
				else {
					cout << "Found file: " << ent->d_name << endl;
					filepaths.push_back(input + "/" + ent->d_name);
				}
			}
			closedir(dir);
		}
		else {
			cout << "Found file: " << input << endl;
			filepaths.push_back(input);
		}
	}
	
	auto num_files = filepaths.size();
	cout << "Number of files: " << num_files << endl;


	for (int i = 0; i < num_files; ++i) {
		auto& filepath = filepaths[i];

		string extension = filepath.substr(filepath.length() - 5, filepath.length());
		bool is_frag = extension.compare(".frag") == 0;
		bool is_vert = extension.compare(".vert") == 0;
		bool is_glsl = extension.compare(".glsl") == 0;

		if (!is_frag && !is_vert && !is_glsl) {
			cout << "Skipping file: " << filepath << " because of unsupported extension. File number" << i << " of " << num_files << endl;
			continue;
		}
		else {
			cout << "Loading file: " << filepath << endl;
			cout << i + 1 << " of " << num_files << endl;
		}

		string shader_source;
		try {
			shader_source = read_file(filepath.c_str());
		}
		catch (...) {
			cout << "WARNING: Failed to load file: " << filepath << endl;
			continue;
		}

		string comment = "";
		auto comment_begin = shader_source.find("/*");
		if (comment_begin != string::npos){
			auto comment_end = shader_source.find("*/");
			if (comment_end != string::npos) {
				comment = shader_source.substr(comment_begin, comment_end);
				shader_source.replace(comment_begin, comment_end, "");
			}
		}

		bool is_complete = !is_glsl;
		bool is_fragment = is_frag || is_glsl;

		if (is_fragment) {
			if (force_high_precision) {
				replace_string(shader_source, "mediump ", "highp ");
				replace_string(shader_source, "lowp ", "highp ");
			}
			else if (force_medium_precision) {
				replace_string(shader_source, "highp ", "mediump ");
				replace_string(shader_source, "lowp ", "mediump ");
			}
		}

		glslopt_shader_type type = is_fragment ? kGlslOptShaderFragment : kGlslOptShaderVertex;
		auto shader = shader_input(filepath, shader_source, comment, type, is_complete);
		bool is_gles3 = shader_source.find("#version 300 es") != string::npos;
		if (is_gles3) shaders_gles3.push_back(shader);
		else shaders_gles2.push_back(shader);
	}
	cout << "Number of GLES 2 shaders: " << shaders_gles2.size() << endl;
	cout << "Number of GLES 3 shaders: " << shaders_gles3.size() << endl;

	process_shaders(shaders_gles2, kGlslTargetOpenGLES20);
	process_shaders(shaders_gles3, kGlslTargetOpenGLES30);

	cout << "oglsl is done." << endl;
	return 0;
}
