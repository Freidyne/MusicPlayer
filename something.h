#pragma once
struct wave
{
	char riff[4];
	int filesize;
	char filetypeheader[4];
	int formatchunkmarker; //might want to be a char array
	int formatdatalength;
	short formatType;
	short numofChannels;
	int sampleRate; //this may be a 32 byte integer, so may want to change this, but it seems like it should only be a 32
	int expression1; //normally this would be calculated, but we're only interpreting files, not encoding them...
	short expression2; //same as above comment...
	short bitsPerSample;
	int dataHeader;
	int dataSectionSize;
};

struct
{
	long holder1;
	long holder2;
	long holder3;
	long holder4;
	long holder5;
	long holder6;
	long holder7;
	long holder8;
	long holder9;
}typedef junkholder;

struct junkwave
{
	char riff[4];
	int filesize;
	char filetypeheader[4];
	junkholder junk;
	int formatchunkmarker; //might want to be a char array
	int formatdatalength;
	short formatType;
	short numofChannels;
	int sampleRate; //this may be a 32 byte integer, so may want to change this, but it seems like it should only be a 32
	int expression1; //normally this would be calculated, but we're only interpreting files, not encoding them...
	short expression2; //same as above comment...
	short bitsPerSample;
	int dataHeader;
	int dataSectionSize;
};