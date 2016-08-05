#ifndef UUID_f3785109_1a14_439d_948a_0a031a06e2eb
#define UUID_f3785109_1a14_439d_948a_0a031a06e2eb


typedef struct
{
	int handle;
	int _padding00;
	unsigned long physical_address;
	unsigned int length;
} memclient_attach_dmabuf_param_t;

#define MEMCLIENT_ATTACH_DMABUF		_IOWR('M', 0x01, memclient_attach_dmabuf_param_t)
#define MEMCLIENT_RELEASE_DMABUF	_IOWR('M', 0x02, int)

#define MEMCLIENT_ATTACH_UMP		_IOWR('M', 0x03, memclient_attach_dmabuf_param_t)
#define MEMCLIENT_RELEASE_UMP		_IOWR('M', 0x04, int)


#endif
