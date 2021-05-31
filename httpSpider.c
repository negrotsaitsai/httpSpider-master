#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <arap/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#endif 
#include "httpSpider.h"
#include "linkqueue.h"
#include "trie.h"

#include <setjmp.h>
#include <signal.h>

#define logfile "spiderLog.txt"
const char *httpHeader = "GET %s HTTP/1.0 \r\n" \
             "User-Agent: Mozilla/5.0(compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0 \r\n\r\n";

#ifdef _WIN32
void initSocket()
{
    WSADATA ws;
    WSAStartup(MAKEWORD(2, 0), &ws);
}
void cleanSocket()
{
    WSACleanup();
}
#endif

FILE *flog;
void termSpider();
void handler(int s)
{
    if(s == SIGINT)
    {
        puts("Interruputed");
        fprintf(flog, "Interruputed by user\n");
        termSpider();
        exit(0);
    }else if(s == SIGSEGV)
    {
        puts("Abort: Segmentation fault");
        fprintf(flog, "Abort by SIGSEGV: Segmentation fault\n");
        termSpider();
        exit(0);
    }
}
void initSpider()
{
    flog = fopen(logfile, "w");
    signal(SIGINT, handler);
    signal(SIGSEGV, handler);
}
void termSpider()
{
    fflush(flog);
    fclose(flog);
}

#ifdef __linux__
typedef int SOCKET;
#endif
#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif
#ifdef _WIN32
#define close closesocket
#endif
long getIP(const char *host)
{
    long r;
    struct addrinfo *h;
    if(getaddrinfo(host, NULL, NULL, &h))
        return 0;
    else
    {
        //inet_ntop(AF_INET, &(((struct sockaddr_in *)(h->ai_addr))->sin_addr), sp -> host, 16);
        //InetNtop(AF_INET, &(((struct sockaddr_in *)(h->ai_addr))->sin_addr), sp -> host, 16); 
        r = ((struct sockaddr_in *)(h->ai_addr))->sin_addr.s_addr;
    }
    return r;
}

int request(spider *sp, char *host, char *path, int port, char *buffer, int maxb)
{
    SOCKET sokfd;
    struct sockaddr_in sokad;
    int sdlen, rlen;
	
    memset(&sokad, 0, sizeof(sokad));
    sokad.sin_family = AF_INET;     //ipv4
    sokad.sin_port = htons(port);
    if(! sp -> ip)
    {
        if((sokad.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE)
        {
            puts("ip��Ч��������Ϣ��������־");
            fprintf(flog, "-- ip��Ч��������Ϣ:\n  ����:%s ��Դ:%s �˿�:%d\n\n", host, path, port);
            puts("���˳�");
            return -1;
        }
    }else
    {
        sokad.sin_addr.s_addr = sp -> ip;
    }
	
    if((sokfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        puts("����socket������ʧ�ܣ�������Ϣ��������־");
        fprintf(flog, "-- ����socket������ʧ�ܣ�������Ϣ:\n  ����:%s ��Դ:%s �˿�:%d\n\n", host, path, port);
        return -1;
    }
    
    if(connect(sokfd, (struct sockaddr *)&sokad, sizeof(sokad)) < 0)
    {
        puts("����ʧ�ܣ�������Ϣ��������־");
        fprintf(flog, "-- ����ʧ�ܣ�������Ϣ:\n  ����:%s ��Դ:%s �˿�:%d\n\n", host, path, port);
    }
    char *rqbuf = (char *)malloc(sizeof(char) * 512);
    sdlen = sprintf(rqbuf, httpHeader, path);
	
    send(sokfd, rqbuf, sdlen, 0);
    free(rqbuf);
	
    rlen = recv(sokfd, buffer, maxb, 0);
    if(rlen < 0)
    {
        puts("��������ʧ�ܣ�������Ϣ��������־");
        fprintf(flog, "-- ��������ʧ�ܣ�������Ϣ:\n  ����:%s ��Դ:%s �˿�:%d\n\n", host, path, port);
        return -1;
    }
    close(sokfd);
    return rlen;
}

void attachPlug(spiderPlug *p, spider *sp)
{
    #ifdef _WIN32
    HINSTANCE hlib = LoadLibrary(p -> plug);
    p -> nativePointer = (long)hlib;
    if(!hlib)
    {
        puts("�������dllʧ��");
        fprintf(flog, "-- �������dllʧ��, ������Ϣ:\n dll·��:%s\n\n", p -> plug);
        //not return, just a warning
    }
    if(!(sp -> analyzer = (analyzerType)GetProcAddress(hlib, p -> func)))
    {
        printf("����:�Ҳ������dll�еķ�������%s\n", p -> func);
        fprintf(flog, "-- ����:�Ҳ������dll�еķ�������%s:\n dll·��:%s\n\n",p -> func, p -> plug);
    }
    p -> attached = true;
    puts("���dll���سɹ�");
    fprintf(flog, "-- �������dll�ɹ� %s\n\n", p -> plug);
    #endif
}
void detachPlug(spiderPlug *p)
{
    #ifdef _WIN32
    FreeLibrary((HINSTANCE)p -> nativePointer);
    #endif
}

//���ĵ���������
bool processUrl(spider *sp, ansiString *ret, char *str)  //str�� ����·��
{
    if(str[0] == '#')return false;  //�϶����Ǳ�ҳ��...
    int len = strlen(str);
    int xlen;
    if(len > ANSISTRING_MAXLEN) len = ANSISTRING_MAXLEN;
    
    char buf[ANSISTRING_MAXLEN];
    char host[ANSISTRING_MAXLEN];
    char *ptr;
    buf[0] = 0;                     //�����0
    //
    for(int i = 0; i < len; i++)
    {
        if(i+4 < len)  //����Խ��
        {
            if(str[i] == 'h')
                if(str[i+1] == 't')
                    if(str[i+2] == 't')
                        if(str[i+3] == 'p')
                        {
                            i = i+4;  
                            if(i+2 < len)
                            {
                                if(str[i+1] == 's')
                                    if(str[i+2] == ':')
                                        return false;     //���̴�https�ڰ�
                            }
                            if(str[i] == ':')
                            {
                                if(i + 2 < len)
                                {
                                    if(str[i+1] == '/')
                                        if(str[i+2] == '/')
                                        {
                                            i = i+3;     //
                                            while(i < len && str[i] == ' ')i++;
                                            int j = 0;
                                            while(i < len && str[i] != '/') //ֱ���ҵ������һ���ָ���
                                                host[j++] = str[i++];
                                            host[j] = 0;
                                            
                                            //����������·��
                                            j = 0;
                                            while(i < len && j < ANSISTRING_MAXLEN)
                                                buf[j++] = str[i++];
                                            buf[j] = 0;
                                            xlen = j;
                                            break;   //��ɣ�����(�������for����)
                                        }
                                }
                            }
                        }
        }
    }
    if(buf[0])    //�ҵ�����·���е����·��
    {
        long tmp;
        if((tmp = getIP(host)) != 0 && tmp != sp -> ip)return false;  //���Ǳ���վ(ip��һ��)
        if(tmp == 0)return false;
        ptr = buf;
    }else
        ptr = str;
    int nl = strlen(ptr);
    for(int i = nl-1; i > 1 && ptr[i] == ' '; i--)   //ȥ������� / 
        ptr[i] = 0;
    //����
    nl = strlen(ptr);
    initAnsiString2(ret, ptr, nl);
    
    //return !existWord(&sp -> slot, str, strlen(str));
    //initAnsiString2(ret, str, len);
    return true;
}

#define BUF_MAXSZ 100000
void bfs(spider *sp)
{
    char *data = (char *)malloc(sizeof(char) * BUF_MAXSZ);
    char pathb[ANSISTRING_MAXLEN];
    puts("��ʼ����");
    fprintf(flog, "-- ��ʼ����\n\n");
    
    linkQueue q;
    initQueue(&q);
    initTrie(&sp -> slot);
    
    ansiString root;
    initAnsiString(&root, "/");
    insertWord(&sp -> slot, "/", 1);
    pushQueue(&q, root);
    
    //��Ҫ���أ��ֵ�����������
    
    while(q.size > 0)
    {
        ansiString curl = popQueue(&q);
        int len = request(sp, sp -> host, curl.buffer, 80, data, BUF_MAXSZ);
        if(len < 0)continue;
        //������ҳԴ���룬��չ��һ���
        
        char *ps = data;
        for(int i = 0; i < len; i++)
        {
            int bk = 1;
            if(ps[i] == '<')
                bk++;
            else if(ps[i] == '>')
                bk--;
            if(bk)      //��html�ı�ǩ����
            {
                if(i+4 >= len)continue;
                if(ps[i] == 'h')                 //peek h
                    if(ps[i+1] == 'r')           //peek r
                        if(ps[i+2] == 'e')       //peek e
                            if(ps[i+3] == 'f')   //peek f
                            {
                                i = i+4;
                                while(i < len && ps[i] != '=')i++;
                                while(i < len && ps[i] != '\"')i++;  // href = "
                                i++;
                                while(i < len && ps[i] == ' ')i++;    //���Զ���ո�
                                
                                //�ҵ�����
                                int j = 0;
                                while(i < len && j < ANSISTRING_MAXLEN && ps[i] != '\"')
                                    pathb[j++] = ps[i++];
                                pathb[j] = 0;
                                
                                ansiString res;
                                if(processUrl(sp, &res, pathb)); //�˵����Ϸ���url����������������е�bug���ǲ���Ӱ�칦��...(��)
                                {
                                    if(existWord(&sp -> slot, res.buffer, res.length))continue;;
                                    //fprintf(flog, "\n%s \n", res.buffer);
                                    insertWord(&sp -> slot, pathb, j);
                                    pushQueue(&q, res);
                                    
                                }
                            }
            }
        }
        if(sp -> analyzer)
            sp -> analyzer(data, len);
        
        printf("ҳ��%s ������ϣ� ������һ��ҳ��\n", curl.buffer);
        destroyAnsiString(&curl);
       
    }
     
    destroyQueue(&q);
    destroyTrie(&sp -> slot);
    free(data);
}

void useDomain(spider *sp)
{
    if(!(sp -> ip = getIP(sp -> host)))
    {
        printf("��������: ����%s �޷�ʶ����ip��ַ!\n", sp -> host);
        fprintf(flog, "��������: ����%s �޷�ʶ����ip��ַ!\n\n", sp -> host);
        puts("���˳�");
        exit(0);
    }
}

int main(int argc, char *argv[])
{
    #ifdef _WIN32
    initSocket();
    #endif
    initSpider(); 
	
    spider sp;
    spiderPlug pg;
    
    memset(&pg, 0, sizeof(spiderPlug));
    memset(&sp, 0, sizeof(spider));
    strcpy(pg.func, "analyzer");     //����ʱ�ն����ˣ��Ժ��Ϊ��ȡconfig�ļ�
    strcpy(pg.plug, "plugin.dll");
    attachPlug(&pg, &sp);
    
    // -----ʵ��׶�
    if(argc > 1)
    {
        strcpy(sp.host, argv[1]);
        useDomain(&sp);
        sp.port = 80;          //��ʱ�յ�Ϊ80����
        bfs(&sp);
    }else
    {
        puts("���������");
    }
    //-------
    
    detachPlug(&pg);
    termSpider();
    #ifdef _WIN32
    cleanSocket();
    #endif
    return 0;
}