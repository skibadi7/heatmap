
heatmap : heatmap.c
	gcc -O3 -Wall -Werror heatmap.c -o heatmap

clean :
	- rm heatmap
