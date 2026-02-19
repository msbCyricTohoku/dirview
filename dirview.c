#define _GNU_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define KNRM "\x1B[0m"
#define KCYN "\x1B[36m"
#define KBLU "\x1B[34m"
#define KGRN "\x1B[32m"

#define ICON_DIR "📁"
#define ICON_FILE "📝"
#define ICON_EXE "🔨"

int comp(const void *a, const void *b) {
  const struct dirent *const *pa = a;
  const struct dirent *const *pb = b;
  return strcmp((*pa)->d_name, (*pb)->d_name);
}

static struct dirent **fileList = NULL; /* array of pointers */
static int cap = 10;                    /* allocated slots   */
static int count = 0;                   /* used slots        */
static char *path = NULL;               /* current directory */
static bool hidden = false;             /* show dot‑files?   */
static int cur_idx = 0;                 /* highlighted entry */
static int offset = 0;                  /* first entry on screen */
char editor[1024];
char image_viewer[1024];
char pdf_reader[1024];

static void adjust_offset(void) {
  int rows, cols;
  getmaxyx(stdscr, rows, cols); /* size of the window      */
  int max_visible = rows - 3;   /* lines we can draw       */

  if (cur_idx < offset) /* cursor went above view */
    offset = cur_idx;
  else if (cur_idx >= offset + max_visible) /* cursor went below view */
    offset = cur_idx - max_visible + 1;
}

void trim_config(char *str) {
  //  int i, j;
  //  for (i = 0, j = 0; str[i]; i++) {
  //    if (!isspace((unsigned char)str[i])) {
  //     str[j++] = str[i];
  //   }
  //  }
  // str[j] = '\0';

  if (str == NULL) return;
   
  int len = strlen(str);
    int i = len - 1;

    while (i >= 0 && isspace((unsigned char)str[i])) {
        i--;
    }
    //str[i + 2] = '\0';
        str[i + 1] = '\0';
}

static int load_dir(void) {
  DIR *dp;
  struct dirent *entry;

  offset = 0;

  for (int i = 0; i < count; ++i)
    free(fileList[i]);
  free(fileList);
  fileList = NULL;
  cap = 10;
  count = 0;
  cur_idx = 0;

  dp = opendir(path);
  if (dp == NULL) {
    perror("opendir");
    return -1;
  }

  fileList = malloc(cap * sizeof(struct dirent *));
  if (!fileList) {
    perror("malloc");
    closedir(dp);
    return -1;
  }

  while ((entry = readdir(dp)) != NULL) {
    if (!hidden && entry->d_name[0] == '.' && strcmp(entry->d_name, ".") != 0 &&
        strcmp(entry->d_name, "..") != 0) {
      continue;
    }

    if (count >= cap) {
      cap *= 2;
      fileList = realloc(fileList, cap * sizeof(struct dirent *));
      if (!fileList) {
        perror("realloc");
        closedir(dp);
        return -1;
      }
    }

    fileList[count] = malloc(sizeof(struct dirent));
    if (!fileList[count]) {
      perror("malloc entry");
      closedir(dp);
      return -1;
    }
    memcpy(fileList[count], entry, sizeof(struct dirent));
    ++count;
  }
  closedir(dp);

  /* sort alphabetically */
  qsort(fileList, count, sizeof(struct dirent *), comp);
  return 0;
}

static void about(void){
  int rows, cols;
  getmaxyx(stdscr, rows, cols);
  erase();
  curs_set(0);
  char dev[] = "developed by msb -- 2026";
  mvprintw(rows/2, cols/2-(int)strlen(dev)/2, "%s",dev);
  refresh();

  getch();
  curs_set(1);
  endwin();
}

static void draw(void) {
  int rows, cols;
  getmaxyx(stdscr, rows, cols);

  //  printf("%d",rows);

  int max_visible = rows - 2; /* how many entries fit */

  erase();
  init_pair(1, COLOR_BLUE, COLOR_BLACK);
  attron(COLOR_PAIR(1));
  mvprintw(0, 1, "%s", path);
  attroff(COLOR_PAIR(1));

  for (int i = 0; i < max_visible && (i + offset) < count; ++i) {
    const struct dirent *e = fileList[i + offset];
    const char *icon = (e->d_type == DT_DIR) ? ICON_DIR : ICON_FILE;
    const char *color = (e->d_type == DT_DIR) ? KBLU : KCYN;

    if (e->d_type != DT_DIR) {
      struct stat st;
      char full[PATH_MAX];
      snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
      if (stat(full, &st) == 0 && (st.st_mode & S_IXUSR)) {
        icon = ICON_EXE;
        color = KGRN;
      }
    }

    if ((i + offset) == cur_idx) /* highlight current line */
      attron(A_REVERSE);

    // attron(COLOR_PAIR(1));
    mvprintw(i + 2, 0, "%s %-*s", icon, cols - 4, e->d_name);
    // attroff(COLOR_PAIR(1));
    attroff(A_REVERSE);
  }

  trim_config(editor);
  mvprintw(rows - 1, 0,
           "DirView: [up/down]/[Enter]/[Backspace]/[h]/[a]/[[q]"); // | Editor: %s ",
                                                             // prog);
  //mvprintw(0, 0,"%s", path);
  refresh();
}

static void activate(void) {
  if (count == 0)
    return;

  struct dirent *e = fileList[cur_idx];
  char newpath[PATH_MAX];

  if (e->d_type == DT_DIR) {
    if (strcmp(e->d_name, "..") == 0) {
      char *last = strrchr(path, '/');
      if (last && last != path) {
        *last = '\0';
      } else {
        strcpy(path, "/");
      }
    } else if (strcmp(e->d_name, ".") != 0) {
      snprintf(newpath, sizeof(newpath), "%s/%s", path, e->d_name);
      free(path);
      path = strdup(newpath);
    }
    load_dir();
  }

  // else {
  //  snprintf(newpath, sizeof(newpath), "%s/%s", path, e->d_name);
  //   endwin(); /* leave ncurses */
  //  trim_config(prog);
  //      execlp(prog, prog, newpath, (char *)NULL);
  //     perror("execlp");
  //      refresh();
  // }

  else {

    char prog_chosen[1024]; 

      snprintf(newpath, sizeof(newpath), "%s/%s", path, e->d_name);

    //    prog = (strcmp(e->d_name, "image.png") == 0) ? "feh" : "vim";

    bool notfile = false;
    
    if (strcmp(strrchr(e->d_name, '.'), ".png") == 0 ||
        strcmp(strrchr(e->d_name, '.'), ".jpeg") == 0 ||
      strcmp(strrchr(e->d_name, '.'), ".jpg") == 0 ||
	strcmp(strrchr(e->d_name, '.'), ".JPG") == 0 ){
      strcpy(prog_chosen, image_viewer);
      notfile = true;
    }
    else if(strcmp(strrchr(e->d_name, '.'), ".pdf") == 0)
      {
	      strcpy(prog_chosen, pdf_reader);
      notfile = true;
      }
    else{
      strcpy(prog_chosen, editor);
      notfile = false;
    }

    //    strcpy(prog, (strcmp(strrchr(e->d_name,'.'), ".png") == 0) ? "feh" :
    //    "vim");
    //        strcpy(prog, (strcmp(strrchr(e->d_name,'.'), ".jpeg") == 0) ?
    //        "feh" : "vim");

    /* save curr ncurses settings */
    def_prog_mode();

    /* temp exit of ncurses so terminal goes norm mode */
    if(!notfile) endwin();

    trim_config(prog_chosen);

    /* process fork */
    pid_t pid = fork();

    if (pid == 0) {

      execlp(prog_chosen, prog_chosen, newpath, (char *)NULL);

      perror("execlp");
      _exit(1);

    } else if (pid > 0) {
      /* parent process */
      int status;
      if(!notfile) waitpid(pid, &status, 0);

      /* restore ncurses dirview */
      reset_prog_mode();
      touchwin(stdscr);
      refresh();
    } else {
      perror("fork");
    }
  }
}

static void handle_input(void) {
  int ch;
  while ((ch = getch()) != 'q') {
    switch (ch) {
    case KEY_UP:
      if (cur_idx > 0)
        --cur_idx;
      break;
    case KEY_DOWN:
      if (cur_idx < count - 1)
        ++cur_idx;
      break;
    case KEY_PPAGE: /* page up   */
      cur_idx -= 10;
      if (cur_idx < 0)
        cur_idx = 0;
      break;
    case KEY_NPAGE: /* page down */
      cur_idx += 10;
      if (cur_idx >= count)
        cur_idx = count - 1;
      break;
    case KEY_HOME:
      cur_idx = 0;
      break;
    case KEY_END:
      cur_idx = count - 1;
      break;
    case '\n': /* enter */
      activate();
      break;
    case KEY_BACKSPACE:
    case 127: /* backspace */
    {
      struct dirent fake = {.d_type = DT_DIR, .d_name = ".."};
      fileList[count] = malloc(sizeof(struct dirent));
      memcpy(fileList[count], &fake, sizeof(struct dirent));
      ++count;
      cur_idx = count - 1;
      activate();
    } break;
    case 'h':
    case 'H':
      hidden = !hidden;
      load_dir();
      break;
    default:
      break;
    case 'a':
    case 'A':
      about();
      break;
    }
    adjust_offset();
    draw();
  }
}

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");

  FILE *config;

  config = fopen("config.in", "r");

  fgets(editor, sizeof(editor), config);
  fgets(image_viewer, sizeof(image_viewer), config);
  fgets(pdf_reader, sizeof(pdf_reader), config);

  trim_config(editor);
  trim_config(image_viewer);
  trim_config(pdf_reader);
  
  printf("%s\n", editor);
  printf("%s\n", image_viewer);
  printf("%s\n", pdf_reader);

  fclose(config);

  if (argc == 1) {
    path = strdup(".");
  } else if (argc == 2) {
    path = strdup(argv[1]);
  } else if (argc == 3) {
    path = strdup(argv[1]);
    if (strcmp(argv[2], "h") == 0)
      hidden = true;
  } else {
    fprintf(stderr, "Usage: %s [path] [h]\n", argv[0]);
    return EXIT_FAILURE;
  }

  if (load_dir() != 0)
    return EXIT_FAILURE;

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  start_color();

  init_pair(1, COLOR_CYAN, COLOR_BLACK);  /* files  */
  init_pair(2, COLOR_BLUE, COLOR_BLACK);  /* dirs   */
  init_pair(3, COLOR_GREEN, COLOR_BLACK); /* exe    */

  draw();
  handle_input();
  

  endwin();

  for (int i = 0; i < count; ++i)
    free(fileList[i]);
  free(fileList);
  free(path);
  return EXIT_SUCCESS;
}
