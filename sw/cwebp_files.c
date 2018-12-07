#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/stat.h>

#define timediff_usec(t0, t1)						\
	((double)(((t0)->tv_sec * 1000000 + (t0)->tv_usec) -		\
		  ((t1)->tv_sec * 1000000 + (t1)->tv_usec)))

int main(int argc, const char *argv[])
{
	const char *in_dir = NULL;
	int c;
	for (c = 1; c < argc; ++c) {
	  if (!strcmp(argv[c], "-i") && c < argc - 1) {
		in_dir = argv[++c];
	  } 
	}
	
	if (in_dir == NULL) {
	  fprintf(stderr, "No input dir specified!\n");
	  return -1;
	}
	
	DIR *dir = NULL;  
	struct dirent *entry;  
	
	dir = opendir(in_dir);
	
	if(dir == NULL){
	  fprintf(stderr, "opendir failed!\n");
	  return -1;
	}

	struct timeval etime, stime;
	
	gettimeofday(&stime, NULL);

	char creat_dir[256] = {0};
	int dir_len;
	sprintf(creat_dir, "%swebp/", in_dir);
	dir_len = strlen(creat_dir);
	mkdir(creat_dir, S_IRWXU);

	while((entry = readdir(dir)) != NULL){
	  if(entry->d_type == 8){ 
		char* dot;
		char in_dir_file[256] = {0};
		char out_dir_file[256] = {0};
		char command[256] = {0};
		
		//input file 
		sprintf(in_dir_file, "%s%s", in_dir, entry->d_name);
		
		//output file
		dot = strrchr(entry->d_name, '.');
		memcpy(out_dir_file, creat_dir, dir_len);
		memcpy(out_dir_file + dir_len, entry->d_name, strlen(entry->d_name)-strlen(dot));
		strcat(out_dir_file, ".webp");
		
		sprintf(command,"cwebp %s -o %s -v",in_dir_file,out_dir_file);
		
		system(command);

	  }
	}
	
	gettimeofday(&etime, NULL);
	
    fprintf(stdout, "All picture coding took %lld usec\n",
    (long long)timediff_usec(&etime, &stime));
	
	closedir(dir); 	  
	
	exit(0);
}

