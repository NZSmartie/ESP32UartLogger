#ifndef HTTP_H__
#define HTTP_H__


void http_link_up(void);
void http_link_down(void);
void http_init(void);

void http_post_data(const char* url, const char* data);


#endif /* HTTP_H__ */