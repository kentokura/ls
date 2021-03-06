/**
 * @file ls11.c
 *
 * Copyright (c) 2015 大前良介 (OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 *
 * @brief lsコマンド風のプログラムを作るNo.11
 * 再帰呼び出しの改善
 *
 * @author <a href="mailto:ryo@mm2d.net">大前良介 (OHMAE Ryosuke)</a>
 * @date 2015/11/24
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#define PATH_MAX 4096
#define HALF_YEAR_SECOND (365 * 24 * 60 * 60 / 2)

#ifndef S_IXUGO
#define S_IXUGO (S_IXUSR | S_IXGRP | S_IXOTH)
#endif

/**
 * 隠しファイルの表示方針
 */
enum {
  FILTER_DEFAULT, /**< '.'から始まるもの以外を表示する */
  FILTER_ALMOST,  /**< '.'と'..'以外を表示する */
  FILTER_ALL,     /**< すべて表示する */
};

/**
 * 再帰呼び出しのためのディレクトリ名を保持するリンクリスト
 */
struct dir_path {
  char path[PATH_MAX + 1];
  int depth;
  struct dir_path *next;
};

static void *xmalloc(size_t size);
static char *parse_cmd_args(int argc, char**argv);
static void get_mode_string(mode_t mode, char *str);
static void print_type_indicator(mode_t mode);
static void print_user(uid_t uid);
static void print_group(gid_t gid);
static void get_time_string(char *str, time_t time);
static void print_name_with_color(const char *name, mode_t mode, bool link_ok);
static struct dir_path *new_dir_path(const char *path, int depth, struct dir_path *next);
static void list_dir(struct dir_path *base);

/**
 * 隠しファイルの表示方針
 */
static int filter = FILTER_DEFAULT;
/**
 * 色付き表示する
 */
static bool color = false;
/**
 * 属性を示す文字を表示する
 */
static bool classify = false;
/**
 * ロングフォーマットで表示する
 */
static bool long_format = false;
/**
 * 半年前のUNIX時間
 */
static time_t half_year_ago;
/**
 * 再帰的な表示
 */
static bool recursive = false;

/**
 * @brief malloc結果がNULLだった場合にexitする。
 * @param[IN] size 確保サイズ
 * @retrun 確保された領域へのポインタ
 */
static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (p == NULL) {
    perror("");
    exit(EXIT_FAILURE);
  }
  return p;
}

/**
 * @brief コマンドライン引数をパースする
 * @param[IN] argc 引数の数
 * @param[IN/OUT] argv 引数配列
 * @return パス
 */
static char *parse_cmd_args(int argc, char**argv) {
  char *path = "./";
  int opt;
  const struct option longopts[] = {
      { "all", no_argument, NULL, 'a' },
      { "almost-all", no_argument, NULL, 'A' },
      { "color", no_argument, NULL, 'C' },
      { "classify", no_argument, NULL, 'F' },
      { "long-format", no_argument, NULL, 'l' },
      { "recursive", no_argument, NULL, 'R' },
  };
  while ((opt = getopt_long(argc, argv, "aACFlR", longopts, NULL)) != -1) {
    switch (opt) {
      case 'a':
        filter = FILTER_ALL;
        break;
      case 'A':
        filter = FILTER_ALMOST;
        break;
      case 'C':
        if (isatty(STDOUT_FILENO)) {
          color = true;
        }
        break;
      case 'F':
        classify = true;
        break;
      case 'l':
        long_format = true;
        half_year_ago = time(NULL) - HALF_YEAR_SECOND;
        break;
      case 'R':
        recursive = true;
        break;
      default:
        return NULL;
    }
  }
  if (argc > optind) {
    path = argv[optind];
  }
  return path;
}

/**
 * @brief モード文字列を作成する
 * @param[IN]  mode モードパラメータ
 * @param[OUT] str  文字列の出力先、11バイト以上のバッファを指定
 */
static void get_mode_string(mode_t mode, char *str) {
  str[0] = (S_ISBLK(mode))  ? 'b' :
           (S_ISCHR(mode))  ? 'c' :
           (S_ISDIR(mode))  ? 'd' :
           (S_ISREG(mode))  ? '-' :
           (S_ISFIFO(mode)) ? 'p' :
           (S_ISLNK(mode))  ? 'l' :
           (S_ISSOCK(mode)) ? 's' : '?';
  str[1] = mode & S_IRUSR ? 'r' : '-';
  str[2] = mode & S_IWUSR ? 'w' : '-';
  str[3] = mode & S_ISUID ? (mode & S_IXUSR ? 's' : 'S') : (mode & S_IXUSR ? 'x' : '-');
  str[4] = mode & S_IRGRP ? 'r' : '-';
  str[5] = mode & S_IWGRP ? 'w' : '-';
  str[6] = mode & S_ISGID ? (mode & S_IXGRP ? 's' : 'S') : (mode & S_IXGRP ? 'x' : '-');
  str[7] = mode & S_IROTH ? 'r' : '-';
  str[8] = mode & S_IWOTH ? 'w' : '-';
  str[9] = mode & S_ISVTX ? (mode & S_IXOTH ? 't' : 'T') : (mode & S_IXOTH ? 'x' : '-');
  str[10] = '\0';
}

/**
 * @brief ファイルタイプ別のインジケータを出力する
 * @param[IN] mode モードパラメータ
 */
static void print_type_indicator(mode_t mode) {
  if (S_ISREG(mode)) {
    if (mode & S_IXUGO) {
      putchar('*');
    }
  } else {
    if (S_ISDIR(mode)) {
      putchar('/');
    } else if (S_ISLNK(mode)) {
      putchar('@');
    } else if (S_ISFIFO(mode)) {
      putchar('|');
    } else if (S_ISSOCK(mode)) {
      putchar('=');
    }
  }
}

/**
 * @brief ユーザ名を表示する
 * @param[IN] uid ユーザID
 */
static void print_user(uid_t uid) {
  struct passwd *passwd = getpwuid(uid);
  if (passwd != NULL) {
    printf("%8s ", passwd->pw_name);
  } else {
    printf("%8d ", uid);
  }
}

/**
 * @brief グループ名を表示する
 * @param[IN] gid グループID
 */
static void print_group(gid_t gid) {
  struct group *group = getgrgid(gid);
  if (group != NULL) {
    printf("%8s ", group->gr_name);
  } else {
    printf("%8d ", gid);
  }
}

/**
 * @brief 時刻表示文字列を作成する
 * 半年以上前の場合は月-日 年
 * 半年以内の場合は月-日 時:分
 *
 * @param[OUT] str  格納先、12byte以上のバッファを指定
 * @param[IN]  time 文字列を作成するUNIX時間
 */
static void get_time_string(char *str, time_t time) {
  if (time - half_year_ago > 0) {
    strftime(str, 12, "%m/%d %H:%M", localtime(&time));
  } else {
    strftime(str, 12, "%m/%d  %Y", localtime(&time));
  }
}

/**
 * @brief ファイル名を色付き表示する
 *
 * @param[IN] name ファイル名
 * @param[IN] mode mode値
 * @param[IN] link_ok リンク先が存在しない場合にfalse
 */
static void print_name_with_color(const char *name, mode_t mode, bool link_ok) {
  if (!link_ok) {
    printf("\033[31m");
  } else if (S_ISREG(mode)) {
    if (mode & S_ISUID) {
      printf("\033[37;41m");
    } else if (mode & S_ISGID) {
      printf("\033[30;43m");
    } else if (mode & S_IXUGO) {
      printf("\033[01;32m");
    } else {
      printf("\033[0m");
    }
  } else if (S_ISDIR(mode)) {
    if ((mode & S_ISVTX) && (mode & S_IWOTH)) {
      printf("\033[30;42m");
    } else if (mode & S_IWOTH) {
      printf("\033[34;42m");
    } else if (mode & S_ISVTX) {
      printf("\033[37;44m");
    } else {
      printf("\033[01;34m");
    }
  } else if (S_ISLNK(mode)) {
    printf("\033[01;36m");
  } else if (S_ISFIFO(mode)) {
    printf("\033[33m");
  } else if (S_ISSOCK(mode)) {
    printf("\033[01;35m");
  } else if (S_ISBLK(mode)) {
    printf("\033[01;33m");
  } else if (S_ISCHR(mode)) {
    printf("\033[01;33m");
  }
  printf("%s", name);
  printf("\033[0m");
}

/**
 * @brief struct subdirのファクトリーメソッド
 * @param[IN] path パス
 * @param[IN] depth 深さ
 * @param[IN] next 次の要素へのポインタ
 * @return struct subdirへのポインタ
 */
static struct dir_path *new_dir_path(const char *path, int depth, struct dir_path *next) {
  struct dir_path *s = xmalloc(sizeof(struct dir_path));
  if (path != NULL) {
    strncpy(s->path, path, sizeof(s->path));
  }
  s->depth = depth;
  s->next = next;
  return s;
}

/**
 * @brief 指定パスのディレクトリエントリをリストする
 * @param[IN] base パス
 */
static void list_dir(struct dir_path *base) {
  const char *base_path = base->path;
  DIR *dir;
  struct dirent *dent;
  char path[PATH_MAX + 1];
  size_t path_len;
  struct dir_path *subque = base;
  dir = opendir(base_path);
  if (dir == NULL) {
    perror(base_path);
    return;
  }
  path_len = strlen(base_path);
  if (path_len >= PATH_MAX - 1) {
    fprintf(stderr, "too long path\n");
    return;
  }
  strncpy(path, base_path, PATH_MAX);
  if (path[path_len - 1] != '/') {
    path[path_len] = '/';
    path_len++;
    path[path_len] = '\0';
  }
  while ((dent = readdir(dir)) != NULL) {
    struct stat dent_stat;
    struct stat link_stat;
    char link[PATH_MAX + 1];
    bool link_ok = true;
    int link_len = 0;
    const char *name = dent->d_name;
    if (filter != FILTER_ALL
        && name[0] == '.'
        && (filter == FILTER_DEFAULT
            || name[1 + (name[1] == '.')] == '\0')) {
      continue;
    }
    strncpy(&path[path_len], dent->d_name, PATH_MAX - path_len);
    if (lstat(path, &dent_stat) != 0) {
      perror(path);
      continue;
    }
    if (recursive && S_ISDIR(dent_stat.st_mode)) {
      if (!(name[0] == '.' && name[1 + (name[1] == '.')] == '\0')) {
        subque->next = new_dir_path(path, base->depth + 1, subque->next);
        subque = subque->next;
      }
    }
    if (S_ISLNK(dent_stat.st_mode)) {
      link_len = readlink(path, link, PATH_MAX);
      if (link_len > 0) {
        link[link_len] = 0;
      }
      if (stat(path, &link_stat) != 0) {
        link_ok = false;
      }
    }
    if (long_format) {
      char buf[12];
      get_mode_string(dent_stat.st_mode, buf);
      printf("%s ", buf);
      printf("%3d ", (int)dent_stat.st_nlink);
      print_user(dent_stat.st_uid);
      print_group(dent_stat.st_gid);
      if (S_ISCHR(dent_stat.st_mode) || S_ISBLK(dent_stat.st_mode)) {
        printf("%4d,%4d ", major(dent_stat.st_rdev), minor(dent_stat.st_rdev));
      } else {
        printf("%9ld ", dent_stat.st_size);
      }
      get_time_string(buf, dent_stat.st_mtim.tv_sec);
      printf("%s ", buf);
    }
    if (color) {
      print_name_with_color(name, dent_stat.st_mode, link_ok);
    } else {
      printf("%s", name);
    }
    if (classify) {
      print_type_indicator(dent_stat.st_mode);
    }
    if (long_format) {
      if (link_len > 0) {
        printf(" -> ");
        if (color) {
          print_name_with_color(link, link_stat.st_mode, link_ok);
        } else {
          printf("%s", link);
        }
      }
    }
    putchar('\n');
  }
  closedir(dir);
}

int main(int argc, char**argv) {
  struct dir_path *head;
  char *path = parse_cmd_args(argc, argv);
  if (path == NULL) {
    return EXIT_FAILURE;
  }
  head = new_dir_path(path, 0, NULL);
  while(head != NULL) {
    if (head->depth != 0) {
      printf("\n%s:\n", head->path);
    }
    list_dir(head);
    struct dir_path *tmp = head;
    head = head->next;
    free(tmp);
  }
  return EXIT_SUCCESS;
}
