#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Simplifed xv6 shell.

#define MAXARGS 10

// All commands have at least a type. Have looked at the type, the code
// typically casts the *cmd to some specific cmd type.
struct cmd {
  int type;          //  ' ' (exec), | (pipe), '<' or '>' for redirection
};

struct execcmd {
  int type;              // ' '
  char *argv[MAXARGS];   // arguments to the command to be exec-ed
};

struct redircmd {
  int type;          // < or > 
  struct cmd *cmd;   // the command to be run (e.g., an execcmd)
  char *file;        // the input/output file
  int flags;         // flags for open() indicating read or write
  int fd;            // the file descriptor number to use for the file
};

struct pipecmd {
  int type;          // |
  struct cmd *left;  // left side of pipe
  struct cmd *right; // right side of pipe
};

int fork1(void);  // Fork but exits on failure.
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2], r;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    _exit(0);
  
  switch(cmd->type){
  default:
    fprintf(stderr, "unknown runcmd\n");
    _exit(-1);

  case ' ':
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      _exit(0);
    // fprintf(stderr, "exec not implemented\n");
    // Your code here ...
    // if (execv(ecmd->argv[0], ecmd->argv) == -1) {
    //     fprintf(stderr, "execv %s failed\n", ecmd->argv[0]);
    //     _exit(-1);
    // }
    // shell不轻易exit
    execv(ecmd->argv[0], ecmd->argv);
    fprintf(stderr, "exec %s failed\n", ecmd->argv[0]);
    break;

  case '>':
  case '<':
    rcmd = (struct redircmd*)cmd;
    // fprintf(stderr, "redir not implemented\n");
    // Your code here ...
    // Make sure you print an error message if one of the system calls you are using fails.
    // A common error is to forget to specify the permission with which the file must be created (i.e., the 3rd argument to open).
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->flags, 0664) < 0) {
        fprintf(stderr, "open %s failed\n", rcmd->file);
        _exit(-1);
    }
    runcmd(rcmd->cmd);
    break;

  case '|':
    pcmd = (struct pipecmd*)cmd;
    // fprintf(stderr, "pipe not implemented\n");
    // Your code here ...
    // pipe()  creates a pipe, a unidirectional data channel that can be used for interprocess communication.
    // pipefd[0] refers to the read end of the pipe.  pipefd[1] refers to the write end of the pipe.
    if (pipe(p) < 0) {
        fprintf(stderr, "pipe failed\n");
        _exit(-1);
    }
    if (fork1() == 0) {
        close(1);
        // The dup() system call creates a copy of the file descriptor oldfd, using the lowest-numbered unused file descriptor for the new descriptor.
        // On success, these system calls return the new file descriptor.  On error, -1 is returned, and errno is set appropriately.
        dup(p[1]);
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->left);
    }
    if (fork1() == 0) {
        close(0);
        dup(p[0]);
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->right);
    }
    // If no data is available, a read on a pipe waits for either data to be written
    // or all ﬁle descriptors referring to the write end to be closed; in the latter case,
    // read will return 0, just as if the end of a data ﬁle had been reached.
    // The fact that read blocks until it is impossible for new data to arrive is one reason
    // that it’s important for the child to close the write end of the pipe
    // before executing wc above: if one of wc’s ﬁle descriptors referred to the write end of the pipe,
    // wc would never see end-of-ﬁle.
    close(p[0]);
    close(p[1]);
    wait(NULL);
    wait(NULL);
    break;
  }    
  _exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  if (isatty(fileno(stdin)))
    fprintf(stdout, "6.828$ ");
  memset(buf, 0, nbuf);
  if(fgets(buf, nbuf, stdin) == 0)
    return -1; // EOF
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd, r;

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Clumsy but will have to do for now.
      // Chdir has no effect on the parent if run in the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        fprintf(stderr, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait(&r);
  }
  exit(0);
}

int
fork1(void)
{
  int pid;
  
  pid = fork();
  if(pid == -1)
    perror("fork");
  return pid;
}


// 以下三个函数都是构造函数。
struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, int type)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = type;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->flags = (type == '<') ?  O_RDONLY : O_WRONLY|O_CREAT|O_TRUNC;
  cmd->fd = (type == '<') ? 0 : 1;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '|';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>";

// 这里可识别的token为'<', '|', '>', 或字符串（这里认为非<|>的字符是长度为1的字符串）。
// *q指向token第一个字符，*eq指向token最后一个字符后的一个字节。
// 该函数返回0(eof), '<', '|', '>'或'a'，'a'表示解析到的token是一个字符串。
int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;
  
  s = *ps;
  // 跳过空白符
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '<':
    s++;
    break;
  case '>':
    s++;
    break;
  default:
    // s指向非|<>的字符串，让*q指向该字符串的开头，*eq指向字符串最后一个字符的后一个字节。
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;
  
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

// 偷看，即在仅可以修改*ps跳过空白符的情况下，查看*ps指向的字符串中，第一个非空白字符是否在toks中。
int
peek(char **ps, char *es, char *toks)
{
  char *s;
  
  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);

// make a copy of the characters in the input buffer, starting from s through es.
// null-terminate the copy to make it a string.
char 
*mkcopy(char *s, char *es)
{
  int n = es - s;
  char *c = malloc(n+1);
  assert(c);
  strncpy(c, s, n);
  c[n] = 0;
  return c;
}

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(stderr, "leftovers: %s\n", s);
    exit(-1);
  }
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;
  cmd = parsepipe(ps, es);
  return cmd;
}

// 注意学习这个函数的思路，先处理第一个|的左部，然后递归处理右部，
// 这样就将右部看作一个整体，该整体可以包含|，也可以不包含|，反正递归调用会恰当处理好。
// ls | sort | tail -1
struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es); // 处理左部
  if(peek(ps, es, "|")){ // peek调用结束*ps指向字符'|'的话就进入循环
    gettoken(ps, es, 0, 0); // 让*ps跳过'|'，指向下一个字符
    cmd = pipecmd(cmd, parsepipe(ps, es)); // 递归右部
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){ // 迭代
    tok = gettoken(ps, es, 0, 0); // 取得'<'或'>'
    if(gettoken(ps, es, &q, &eq) != 'a') {
      fprintf(stderr, "missing file for redirection\n");
      exit(-1);
    }
    switch(tok){
    case '<':
      cmd = redircmd(cmd, mkcopy(q, eq), '<'); // `cmd < file`
      break;
    case '>':
      cmd = redircmd(cmd, mkcopy(q, eq), '>'); // `cmd > file`
      break;
    }
  }
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;
  
  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es); // `> x`这样的命令，则创建文件x
  while(!peek(ps, es, "|")) { // 读取参数
    if((tok=gettoken(ps, es, &q, &eq)) == 0) // eof
      break;
    if(tok != 'a') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = mkcopy(q, eq);
    argc++;
    if(argc >= MAXARGS) {
      fprintf(stderr, "too many args\n");
      exit(-1);
    }
    ret = parseredirs(ret, ps, es); // `ls -l > x`
  }
  cmd->argv[argc] = 0; // NULL
  return ret;
}
