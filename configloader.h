#ifndef CONFIGLOADER_H
#define CONFIGLOADER_H

#include<fstream>
#include<stdlib.h>
#include<iostream>

#include <string>
#include <map>
#include <vector>

using namespace std;

class ConfigLoader
{
public:
    ConfigLoader();
    ~ConfigLoader();

    bool loadConfig(const string &filename);
    int readInt(const char *section, const char *key, const int &default_value);
    int getInt(const char *key, const int &default_value);
    float getFloat(const char *key, const float &default_value);
    string getStr(const char *key, const string &default_value);
    void beginSection(const string &section);
    void endSection();

private:
    bool isSpace(char c);
    void trim(string &str);
    bool parseLine(const string &line, string &section, string &key, string &value);

    map<string, map<string, string>> _settings;
    map<string, string> _curSection;
};

#endif
