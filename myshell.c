#include <stdio.h>
#include <stdlib.h>
#include "parser.h"

char buf[1024];
tline* line;

int main(){
	printf("msh> ");
	while (fgets(buf, sizeof(buf), stdin) != NULL){
		
		line = tokenize(buf);
		
		printf("msh> ");
	}
	return 0;
}
