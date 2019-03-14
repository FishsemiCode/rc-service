#include "configloader.h"

ConfigLoader::ConfigLoader()
{

}

ConfigLoader::~ConfigLoader()
{

}

bool ConfigLoader::isSpace(char c)
{
    if (' ' == c || '\t' == c)
        return true;
    return false;
}

void ConfigLoader::trim(string &str)
{
    if(str.empty())
        return;

    int i, start_pos, end_pos;
    for(i = 0; i < (int)str.size(); i++) {
        if(!isSpace(str[i])) {
            break;
        }
    }
    if(i == (int)str.size()) {
        str == "";
        return;
    }
    start_pos = i;
    for(i = (int)str.size() - 1; i >= 0; i--) {
        if(!isSpace(str[i])) {
            break;
        }
    }
    end_pos = i;
    str = str.substr(start_pos, end_pos - start_pos + 1);
}

bool ConfigLoader::parseLine(const string &line, string &section, string &key, string &value)
{
    if(line.empty())
        return false;
    int start_pos = 0;
    int end_pos = line.size()-1;
    int pos, s_startpos, s_endpos;

    /* parse line contains comments */
    if((pos = line.find('#')) != -1) {
        if(0 == pos) {
            return false;
        }
        end_pos = pos - 1;
    }

    /* parse section line contains '[' and ']' */
    if((s_startpos = line.find('[')) != -1 && ((s_endpos = line.find(']')) != -1)) {
        section = line.substr(s_startpos + 1, s_endpos - s_startpos - 1);
        //cout << "section = " << section << endl;
        return true;
    }

    /* parse key-value line */
    string new_line = line.substr(start_pos, end_pos - start_pos + 1);
    if((pos = new_line.find('=')) == -1) {
        /* key-value format error */
        return false;
    }
    key = new_line.substr(0, pos);
    value = new_line.substr(pos + 1, end_pos - pos);
    trim(key);
    if(key.empty())
        return false;
    trim(value);
    if((pos = value.find("\r")) > 0) {
        value.replace(pos, 1, "");
    }
    if((pos = value.find("\n")) > 0) {
        value.replace(pos, 1, "");
    }

    return true;
}

int ConfigLoader::readInt(const char *section, const char *key, const int &default_value)
{
    string tmp_s(section);
    string tmp_k(key);
    map<string, string> k_v;
    map<string, string>::iterator item;
    map<string, map<string, string>>::iterator it;

    it = _settings.find(tmp_s);
    if(it == _settings.end()) {
        return default_value;
    }
    k_v = it->second;
    item = k_v.find(tmp_k);
    if(item == k_v.end()) {
        return default_value;
    }

    return atoi(item->second.c_str());
}

bool ConfigLoader::loadConfig(const string &filename)
{
    _settings.clear();
    ifstream infile(filename.c_str());
    if(!infile) {
        return false;
    }

    string line;
    string section, key, value;
    map<string, string> keyValue;
    map<string, map<string, string>>::iterator it;

    while(getline(infile, line)) {
        if(parseLine(line, section, key, value)) {
            it = _settings.find(section);
            if(it != _settings.end()) {
                keyValue[key] = value;
                it->second = keyValue;
            } else {
                keyValue.clear();
                _settings.insert(make_pair(section, keyValue));
            }
        }
        key.clear();
        value.clear();
    }
    infile.close();
    return true;
}

int ConfigLoader::getInt(const char *key, const int &default_value)
{
    if(_curSection.empty()) {
        return default_value;
    }
    map<string, string>::iterator item;
    item = _curSection.find(key);
    if(item == _curSection.end()) {
        return default_value;
    }

    return atoi(item->second.c_str());
}

float ConfigLoader::getFloat(const char *key, const float &default_value)
{
    if(_curSection.empty()) {
        return default_value;
    }
    map<string, string>::iterator item;
    item = _curSection.find(key);
    if(item == _curSection.end()) {
        return default_value;
    }

    return atof(item->second.c_str());
}

string ConfigLoader::getStr(const char *key, const string &default_value)
{
    if(_curSection.empty()) {
        return default_value;
    }
    map<string, string>::iterator item;
    item = _curSection.find(key);
    if(item == _curSection.end()) {
        return default_value;
    }

    return item->second;
}

void ConfigLoader::beginSection(const string &section)
{
    if(!_curSection.empty()) {
        _curSection.clear();
    }
    _curSection = _settings.find(section)->second;
}

void ConfigLoader::endSection()
{
    _curSection.clear();
}
