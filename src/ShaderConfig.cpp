#include <string>
using std::string;
using std::to_string;
#include <set>
using std::set;
#include <vector>
using std::vector;
#include <cctype> // isalpha
#include <stdexcept>
using std::runtime_error;

#include "JsonFileReader.h"
#include "ShaderConfig.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
namespace rj = rapidjson;

// TODO throw for unknown configuration options

static const string WINDOW_SZ_KEY("window_size");
static const string AUDIO_NUM_FRAMES_KEY("audio_num_frames");

static AudioOptions parse_audio_options(rj::Document& user_conf) {
    AudioOptions ao;

    rj::Value& audio_options = user_conf["audio_options"];
    if (!audio_options.IsObject())
        throw runtime_error("Audio options must be a json object");
    if (!audio_options.HasMember("fft_smooth"))
        throw runtime_error("Audio options must contain the fft_smooth option");
    if (!audio_options.HasMember("wave_smooth"))
        throw runtime_error("Audio options must contain the wave_smooth option");
    if (!audio_options.HasMember("fft_sync"))
        throw runtime_error("Audio options must contain the fft_smooth option");
    if (!audio_options.HasMember("diff_sync"))
        throw runtime_error("Audio options must contain the diff_sync option");

    rj::Value& fft_smooth = audio_options["fft_smooth"];
    rj::Value& wave_smooth = audio_options["wave_smooth"];
    rj::Value& fft_sync = audio_options["fft_sync"];
    rj::Value& diff_sync = audio_options["diff_sync"];

    if (!fft_smooth.IsNumber())
        throw runtime_error("fft_smooth must be a number between in the interval [0, 1]");
    if (!wave_smooth.IsNumber())
        throw runtime_error("wave_smooth must be a number between in the interval [0, 1]");
    if (!fft_sync.IsBool())
        throw runtime_error("fft_sync must be true or false");
    if (!diff_sync.IsBool())
        throw runtime_error("diff_sync must be true or false");

    ao.fft_smooth = fft_smooth.GetFloat();
    if (ao.fft_smooth < 0 || ao.fft_smooth > 1)
        throw runtime_error("fft_smooth must be in the interval [0, 1]");

    ao.wave_smooth = wave_smooth.GetFloat();
    if (ao.wave_smooth < 0 || ao.wave_smooth > 1)
        throw runtime_error("wave_smooth must be in the interval [0, 1]");

    ao.fft_sync = fft_sync.GetBool();
    ao.diff_sync = diff_sync.GetBool();

    return ao;
}

static Buffer parse_image_buffer(rj::Document& user_conf) {
    Buffer image_buffer;
    image_buffer.width = 0;
    image_buffer.height = 0;
    image_buffer.is_window_size = true;

    rj::Value& image = user_conf["image"];
    if (!image.IsObject())
        throw runtime_error("image is not a json object");
    if (!image.HasMember("geom_iters"))
        throw runtime_error("image does not contain the geom_iters option");

    rj::Value& geom_iters = image["geom_iters"];

    if (!geom_iters.IsInt() || geom_iters.GetInt() <= 0)
        throw runtime_error("image.geom_iters must be a positive integer");
    image_buffer.geom_iters = geom_iters.GetInt();

    if (image.HasMember("clear_color")) {
        rj::Value& clear_color = image["clear_color"];
        if (!(clear_color.IsArray() && clear_color.Size() == 3))
            throw runtime_error("image.clear_color must be an array of 3 real numbers each between 0 and 1");
        for (int i = 0; i < 3; ++i) {
            if (clear_color[i].IsNumber())
                image_buffer.clear_color[i] = clear_color[i].GetFloat();
            else
                throw runtime_error("image.clear_color must be an array of 3 real numbers each between 0 and 1");
        }
    }
    else {
        for (int i = 0; i < 3; ++i)
            image_buffer.clear_color[i] = 0.f;
    }

    return image_buffer;
}

static Buffer parse_buffer(rj::Value& buffer, string buffer_name, set<string>& buffer_names) {
    Buffer b;

    b.name = buffer_name;
    if (b.name == string(""))
        throw runtime_error("Buffer must have a name");
    if (buffer_names.count(b.name))
        throw runtime_error("Buffer name " + b.name + " already used (buffers must have unique names)");
    buffer_names.insert(b.name);
    if (!(std::isalpha(b.name[0]) || b.name[0] == '_'))
        throw runtime_error("Invalid buffer name: " + b.name + " buffer names must start with either a letter or an underscore");
    if (b.name == string("image"))
        throw runtime_error("Cannot name buffer image");

    if (!buffer.IsObject())
        throw runtime_error("Buffer " + b.name + " is not a json object");

    if (!buffer.HasMember("size"))
        throw runtime_error(b.name + " does not contain the size option");
    if (!buffer.HasMember("geom_iters"))
        throw runtime_error(b.name + " does not contain the geom_iters option");
    if (buffer.HasMember("clear_color")) {
        rj::Value& b_clear_color = buffer["clear_color"];
        if (!b_clear_color.IsArray() || b_clear_color.Size() != 3) {
            throw runtime_error(b.name + ".clear_color must be an array of 3 real numbers each between 0 and 1");
        }
        for (int i = 0; i < 3; ++i) {
            if (b_clear_color[i].IsNumber())
                b.clear_color[i] = b_clear_color[i].GetFloat();
            else
                throw runtime_error(b.name + ".clear_color must be an array of 3 real numbers each between 0 and 1");
            // else if (b_clear_color[i].IsInt())
            //	b.clear_color[i] = b_clear_color[i].GetFloat()/256.f;
        }
    }
    else {
        for (int i = 0; i < 3; ++i)
            b.clear_color[i] = 0.f;
    }

    rj::Value& b_size = buffer["size"];
    rj::Value& b_geom_iters = buffer["geom_iters"];

    if (b_size.IsArray() && b_size.Size() == 2) {
        if (!b_size[0].IsInt() || !b_size[1].IsInt())
            throw runtime_error(b.name + ".size must be an array of two positive integers");
        b.width = b_size[0].GetInt();
        b.height = b_size[1].GetInt();
        b.is_window_size = false;
    }
    else if (b_size.IsString() && b_size.GetString() == WINDOW_SZ_KEY) {
        b.is_window_size = true;
        b.height = 0;
        b.width = 0;
    }
    else {
        throw runtime_error(b.name + ".size must be an array of two positive integers");
    }

    if (!b_geom_iters.IsInt() || b_geom_iters.GetInt() == 0)
        throw runtime_error(b.name + ".geom_iters must be a positive integer");
    b.geom_iters = b_geom_iters.GetInt();

    return b;
}

static vector<Buffer> parse_buffers(rj::Document& user_conf) {
    vector<Buffer> buffers_vec;

    rj::Value& buffers = user_conf["buffers"];
    if (!buffers.IsObject())
        throw runtime_error("buffers is not a json object");

    if (buffers.MemberCount() == 0) {
        return {};
    }

    // Catch buffers with the same name
    set<string> buffer_names;

    for (auto memb = buffers.MemberBegin(); memb != buffers.MemberEnd(); memb++) {
        buffers_vec.push_back(parse_buffer(memb->value, memb->name.GetString(), buffer_names));
    }

    return buffers_vec;
}

static vector<int> parse_render_order(rj::Document& user_conf, vector<Buffer>& buffers) {
    vector<int> ro;

    if (!user_conf.HasMember("render_order")) {
        for (int i = 0; i < buffers.size(); ++i)
            ro.push_back(i);
        return ro;
    }

    if (!buffers.size()) {
        return {};
    }

    rj::Value& render_order = user_conf["render_order"];
    if (!render_order.IsArray() || render_order.Size() == 0)
        throw runtime_error("render_order must be an array of strings (buffer names) with length > 0");
    for (unsigned int i = 0; i < render_order.Size(); ++i) {
        if (!render_order[i].IsString())
            throw runtime_error("render_order can only contain strings (buffer names)");

        string b_name = render_order[i].GetString();
        int index = -1;
        for (int j = 0; j < buffers.size(); ++j) {
            if (buffers[j].name == b_name) {
                index = j;
                break;
            }
        }
        if (index == -1)
            throw runtime_error("render_order member \"" + b_name + "\" must be the name of a buffer in \"buffers\"");

        // mRender_order contains indices into mBuffers
        ro.push_back(index);
    }

    return ro;
}

static void delete_unused_buffers(vector<Buffer>& buffers, vector<int>& render_order) {
    // Only keep the buffers that are used to render
    set<int> used_buff_indices;
    vector<Buffer> used_buffs;
    for (int i = 0; i < render_order.size(); i++) {
        if (!used_buff_indices.count(render_order[i])) {
            used_buffs.push_back(buffers[render_order[i]]);
            used_buff_indices.insert(render_order[i]);
        }
    }
    buffers = used_buffs;
}

Uniform parse_uniform(rj::Value& uniform, string uniform_name, set<string>& uniform_names) {
    Uniform u;

    if (uniform_names.count(uniform_name))
        throw runtime_error("Uniform name " + uniform_name + " already used (uniforms must have unique names)");
    uniform_names.insert(uniform_name);
    u.name = uniform_name;

    if (uniform.IsArray()) {
        if (uniform.Size() > 4)
            throw runtime_error("Uniform " + u.name + " must have dimension less than or equal to 4");
        for (unsigned int i = 0; i < uniform.Size(); ++i) {
            if (!uniform[i].IsNumber())
                throw runtime_error("Uniform " + u.name + " contains a non-numeric value.");
            u.values.push_back(uniform[i].GetFloat());
        }
    }
    else if (uniform.IsNumber()) {
        u.values.push_back(uniform.GetFloat());
    }
    else {
        throw runtime_error("Uniform " + u.name + " must be either a number or an array of numbers.");
    }
    return u;
}

static vector<Uniform> parse_uniforms(rj::Document& user_conf) {
    vector<Uniform> uniform_vec;

    rj::Value& uniforms = user_conf["uniforms"];
    if (!uniforms.IsObject())
        throw runtime_error("Uniforms must be a json object.");

    if (uniforms.MemberCount() == 0) {
        return {};
    }

    // Catch uniforms with the same name
    set<string> uniform_names;

    for (auto memb = uniforms.MemberBegin(); memb != uniforms.MemberEnd(); memb++) {
        uniform_vec.push_back(parse_uniform(memb->value, memb->name.GetString(), uniform_names));
    }

    return uniform_vec;
}

ShaderConfig::ShaderConfig(const filesys::path& conf_file_path)
    : ShaderConfig(JsonFileReader::read(conf_file_path)) {
    // cannot take const path & because then an implicit conversion causes an infinite recursion. :D
    // filesys::path has string->path implicit conversion constructors for convenience

    // One fix is to declare the constructors explicit and to make the string constructor take a
    // const ref so that the JsonFileReader rvalue reference can bind to it.
}

ShaderConfig::ShaderConfig(const string& json_str) {
    rj::Document user_conf;
    rj::ParseResult ok =
        user_conf.Parse<rj::kParseCommentsFlag | rj::kParseTrailingCommasFlag>(json_str.c_str());
    if (!ok)
        throw runtime_error("JSON parse error: " + string(rj::GetParseError_En(ok.Code())) +
                            " At char offset (" + to_string(ok.Offset()) + ")");

    if (!user_conf.IsObject())
        throw runtime_error("Invalid json file");

    if (user_conf.HasMember("initial_window_size")) {
        rj::Value& window_size = user_conf["initial_window_size"];
        if (!(window_size.IsArray() && window_size.Size() == 2 && window_size[0].IsInt() && window_size[1].IsInt()))
            throw runtime_error("initial_window_size must be an array of 2 positive integers");
        mInitWinSize.width = window_size[0].GetInt();
        mInitWinSize.height = window_size[1].GetInt();
    }
    else {
        mInitWinSize.height = 400;
        mInitWinSize.width = 400;
    }

    if (user_conf.HasMember("audio_enabled")) {
        rj::Value& audio_enabled = user_conf["audio_enabled"];
        if (!audio_enabled.IsBool())
            throw runtime_error("audio_enabled must be true or false");
        mAudio_enabled = audio_enabled.GetBool();
    }
    else {
        mAudio_enabled = true;
    }

    if (user_conf.HasMember("audio_options")) {
        mAudio_ops = parse_audio_options(user_conf);
    }
    else {
        mAudio_ops.diff_sync = true;
        mAudio_ops.fft_sync = true;
        mAudio_ops.fft_smooth = .75;
        mAudio_ops.wave_smooth = .75;
    }

    if (user_conf.HasMember("blend")) {
        if (!user_conf["blend"].IsBool())
            throw runtime_error("Invalid type for blend option");
        mBlend = user_conf["blend"].GetBool();
    }
    else {
        mBlend = false;
    }

    if (user_conf.HasMember("image")) {
        mImage = parse_image_buffer(user_conf);
    }
    else {
        throw runtime_error("shader.json needs the image setting");
    }

    if (user_conf.HasMember("buffers")) {
        mBuffers = parse_buffers(user_conf);
        mRender_order = parse_render_order(user_conf, mBuffers);
        delete_unused_buffers(mBuffers, mRender_order);
    }

    if (user_conf.HasMember("uniforms")) {
        mUniforms = parse_uniforms(user_conf);
    }
}
