
// heatmap: generates a heatmap for a csv file created by rtl_power, for
//          use with the RTL-SDR. Does two passes over the csv file.
//          Does not support axis labels. The color map can be configured
//          with the -c option. The generated image is written to stdout
//          in ppm format and can be converted to png with pnmtopng.
//
// Copyright (C) 2026  skibadi7
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

char* program_name;
char* file_name;
char buffer[1024*1024];
char prev_date[1024];
char prev_time[1024];


#define MAX_TRANSITION_COLORS 64
size_t num_transitions;
double transition_colors[MAX_TRANSITION_COLORS*3];


// Default color map.
void set_transitions_color() {

  // Black
  transition_colors[0] = 0.0;
  transition_colors[1] = 0.0;
  transition_colors[2] = 0.0;

  // Blue
  transition_colors[3] = 0.0;
  transition_colors[4] = 0.0;
  transition_colors[5] = 1.0;

  // Turquoise
  transition_colors[6] = 0.0;
  transition_colors[7] = 0.5;
  transition_colors[8] = 0.5;

  // Green
  transition_colors[9] =  0.0;
  transition_colors[10] = 1.0;
  transition_colors[11] = 0.0;

  // Yellow
  transition_colors[12] = 0.5;
  transition_colors[13] = 0.5;
  transition_colors[14] = 0.0;

  // Red
  transition_colors[15] = 1.0;
  transition_colors[16] = 0.0;
  transition_colors[17] = 0.0;

  // Magenta
  transition_colors[18] = 1.0;
  transition_colors[19] = 0.0;
  transition_colors[20] = 1.0;

  // White
  transition_colors[21] = 1.0;
  transition_colors[22] = 1.0;
  transition_colors[23] = 1.0;

  num_transitions = 8;
}

void set_transitions_grey_scale() {

  // Black
  transition_colors[0] = 0.0;
  transition_colors[1] = 0.0;
  transition_colors[2] = 0.0;

  // White
  transition_colors[3] = 1.0;
  transition_colors[4] = 1.0;
  transition_colors[5] = 1.0;

  num_transitions = 2;
}

int set_transitions_string(char* str) {

  int ret = 1;

  int i = 0;
  for (char* tok = strtok(str, ","); tok != NULL; tok = strtok(NULL, ",")) {

    if (i >= 3*MAX_TRANSITION_COLORS) {
      fprintf(stderr, "%s: too many color values, maximum %d\n", program_name, MAX_TRANSITION_COLORS);
      ret = 0;
      break;
    }

    char* endptr;
    transition_colors[i] = strtod(tok, &endptr);
    if (*endptr) {
      fprintf(stderr, "%s: bad color value %s\n", program_name, tok);
      ret = 0;
      break;
    }
    i++;
  }

  if (ret) {
    if (i % 3 != 0) {
      fprintf(stderr, "%s: %d colors values provided not a multiple of 3\n", program_name, i);
      ret = 0;
    } else {
      num_transitions = i/3;
    }
  }

  return ret;
}


void get_color(double val, int max_val, char* ret_color) {

  double tval = val*(num_transitions-1);
  int index = (int)(tval);
  if (index < 0) index = 0;
  if (index >= num_transitions - 1) index = num_transitions - 2;
  int next_index = index + 1;

  double w2 = tval - index;
  double w1 = 1.0 - w2;

  ret_color[0] = (w1*transition_colors[3*index + 0] + w2*transition_colors[3*next_index + 0])*max_val;
  ret_color[1] = (w1*transition_colors[3*index + 1] + w2*transition_colors[3*next_index + 1])*max_val;
  ret_color[2] = (w1*transition_colors[3*index + 2] + w2*transition_colors[3*next_index + 2])*max_val;
}


char* skip_space(char* str) {

  while (*str == ' ') str++;

  return str;
}


void print_usage() {
  fprintf(stderr, "%s: usage: %s [-c color|grey|r1,g1,b1,r2,g2,b2,...] input.csv\n", program_name, program_name);
}

int main(int argc, char** argv) {

  program_name = argv[0];

  set_transitions_color();

  int ret = 0;

  int i = 1;
  while (i < argc) {
    if (argv[i][0] != '-') break;

    if (!strcmp(argv[i] + 1, "c")) {

      if (i + 1 >= argc) {
        fprintf(stderr, "%s: expected color|grey|r1,g1,b1,r2,g2,b2,... after -c", program_name);
        print_usage();
        ret = 1;
        break;
      }

      if (!strcmp(argv[i+1], "color")) {
        set_transitions_color();
      } else if (!strcmp(argv[i+1], "grey")) {
        set_transitions_grey_scale();
      } else {
        if (!set_transitions_string(argv[i+1])) {
          ret = 1;
          break;
        }
      }

      i += 2;

    } else {
      fprintf(stderr, "%s: bad option %s\n", program_name, argv[i]);
      print_usage();
      ret = 1;
      break;
    }
  }

  if (i != argc - 1) {
    print_usage();
    ret = 1;
  } else {

    file_name = argv[i];

    FILE* input = fopen(file_name, "r");
    if (!input) {
      fprintf(stderr, "%s: could not open %s: %s\n", program_name, file_name, strerror(errno));
      ret = 2;
    } else {

      int line_number = 0;
      double min_sample = 0.0;
      double max_sample = 0.0;
      double min_lower_freq = 0.0;
      double max_upper_freq = 0.0;
      prev_date[0] = prev_time[0] = '\0';

      int num_times = 0;

      int first_sample = 1;
      int num_columns = 0;

      while (fgets(buffer, sizeof(buffer), input) != NULL) {

        char* date_str = strtok(buffer, ",");
        if (!date_str) {
          fprintf(stderr, "%s: Error in file %s, line %d: Expected date\n", program_name, file_name, line_number);
          ret = 3;
          break;
        }

        date_str = skip_space(date_str);

        char* time_str = strtok(NULL, ",");
        if (!time_str) {
          fprintf(stderr, "%s: Error in file %s, line %d: Expected time\n", program_name, file_name, line_number);
          ret = 3;
          break;
        }

        time_str = skip_space(time_str);

        char* lower_freq_str = strtok(NULL, ",");
        if (!lower_freq_str) {
          fprintf(stderr, "%s: Error in file %s, line %d: Expected lower_freq\n", program_name, file_name, line_number);
          ret = 3;
          break;
        }
        lower_freq_str = skip_space(lower_freq_str);

        double lower_freq = strtod(lower_freq_str, NULL);

        char* upper_freq_str = strtok(NULL, ",");
        if (!upper_freq_str) {
          fprintf(stderr, "%s: Error in file %s, line %d: Expected upper_freq\n", program_name, file_name, line_number);
          ret = 3;
          break;
        }
        upper_freq_str = skip_space(upper_freq_str);

        double upper_freq = strtod(upper_freq_str, NULL);

        char* bin_size_str = strtok(NULL, ",");
        if (!bin_size_str) {
          fprintf(stderr, "%s: Error in file %s, line %d: Expected bin_size\n", program_name, file_name, line_number);
          ret = 3;
          break;
        }
        bin_size_str = skip_space(bin_size_str);

        //double bin_size = strtod(bin_size_str, NULL);

        char* num_samples_str = strtok(NULL, ",");
        if (!num_samples_str) {
          fprintf(stderr, "%s: Error in file %s, line %d: Expected num_samples\n", program_name, file_name, line_number);
          ret = 3;
          break;
        }
        num_samples_str = skip_space(num_samples_str);

        // disregarding num_samples.
        //int num_samples = atoi(num_samples_str);

        int actual_num_samples = 0;

        for (;;) {
          char* sample_str = strtok(NULL, ",");
          if (!sample_str) break;
          sample_str = skip_space(sample_str);

          double sample = strtod(sample_str, NULL);

          if (first_sample) {
            min_sample = max_sample = sample;
            first_sample = 0;
          } else {
            if (sample < min_sample) min_sample = sample;
            if (sample > max_sample) max_sample = sample;
          }
          actual_num_samples++;
        }

        if (line_number == 0) {
          min_lower_freq = lower_freq;
          max_upper_freq = upper_freq;
          num_times++;
          num_columns += actual_num_samples;
        } else {
          if (lower_freq < min_lower_freq) min_lower_freq = lower_freq;
          if (upper_freq > max_upper_freq) max_upper_freq = upper_freq;

          int same_time = !strcmp(date_str, prev_date) && !strcmp(time_str, prev_time);
          if (!same_time) {
            num_times++;
          }

          if (num_times <= 1) {
            num_columns += actual_num_samples;
          }
        }

        snprintf(prev_date, sizeof(prev_date), "%s", date_str);
        snprintf(prev_time, sizeof(prev_time), "%s", time_str);
        line_number++;
      }

      fclose(input);

      if (ret == 0) {

        int width = num_columns;
        int height = num_times;

        int max_val = 255;

        fprintf(stderr, "%s: output image dimensions: %dx%d\n", program_name, width, height);
        fprintf(stderr, "%s: min_lower_freq=%lf, max_upper_freq=%lf\n", program_name, min_lower_freq, max_upper_freq);
        fprintf(stderr, "%s: min_sample=%lf, max_sample=%lf\n", program_name, min_sample, max_sample);

        printf("P6\n%d %d\n%d\n", width, height, max_val);

        input = fopen(file_name, "r");
        if (!input) {
          fprintf(stderr, "%s: could not open %s: %s\n", program_name, file_name, strerror(errno));
          ret = 2;
        } else {

          int line_number = 0;
          prev_date[0] = prev_time[0] = '\0';

          while (fgets(buffer, sizeof(buffer), input) != NULL) {

            char* date_str = strtok(buffer, ",");
            if (!date_str) {
              fprintf(stderr, "%s: Error in file %s, line %d: Expected date\n", program_name, file_name, line_number);
              ret = 3;
              break;
            }
            date_str = skip_space(date_str);

            char* time_str = strtok(NULL, ",");
            if (!time_str) {
              fprintf(stderr, "%s: Error in file %s, line %d: Expected time\n", program_name, file_name, line_number);
              ret = 3;
              break;
            }
            time_str = skip_space(time_str);

            char* lower_freq_str = strtok(NULL, ",");
            if (!lower_freq_str) {
              fprintf(stderr, "%s: Error in file %s, line %d: Expected lower_freq\n", program_name, file_name, line_number);
              ret = 3;
              break;
            }
            lower_freq_str = skip_space(lower_freq_str);

            //double lower_freq = strtod(lower_freq_str, NULL);

            char* upper_freq_str = strtok(NULL, ",");
            if (!upper_freq_str) {
              fprintf(stderr, "%s: Error in file %s, line %d: Expected upper_freq\n", program_name, file_name, line_number);
              ret = 3;
              break;
            }
            upper_freq_str = skip_space(upper_freq_str);

            //double upper_freq = strtod(upper_freq_str, NULL);

            char* bin_size_str = strtok(NULL, ",");
            if (!bin_size_str) {
              fprintf(stderr, "%s: Error in file %s, line %d: Expected bin_size\n", program_name, file_name, line_number);
              ret = 3;
              break;
            }
            bin_size_str = skip_space(bin_size_str);

            //double bin_size = strtod(bin_size_str, NULL);

            char* num_samples_str = strtok(NULL, ",");
            if (!num_samples_str) {
              fprintf(stderr, "%s: Error in file %s, line %d: Expected num_samples\n", program_name, file_name, line_number);
              ret = 3;
              break;
            }
            num_samples_str = skip_space(num_samples_str);

            // disregarding num_samples.
            //int num_samples = atoi(num_samples_str);

            //int actual_num_samples = 0;

            for (;;) {
              char* sample_str = strtok(NULL, ",");
              if (!sample_str) break;
              sample_str = skip_space(sample_str);

              double sample = strtod(sample_str, NULL);

              double sample_scaled = (sample - min_sample)/(max_sample - min_sample);
              char color[3];
              get_color(sample_scaled, max_val, color);
              fwrite(color, sizeof(color), 1, stdout);
              //actual_num_samples++;
            }


            snprintf(prev_date, sizeof(prev_date), "%s", date_str);
            snprintf(prev_time, sizeof(prev_time), "%s", time_str);
            line_number++;
          }

          fclose(input);
        }
      }
    }
  }

  return ret;
}
