#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <sys/conf.h>
#include <sys/dnv.h>
#include <sys/malloc.h>
#include <sys/nv.h>
#include <sys/uio.h>
#include <sys/ioccom.h>

#define BUFFER_SIZE 256
static d_open_t echo_open;
static d_close_t echo_close;
static d_read_t echo_read;
static d_write_t echo_write;
static d_ioctl_t echo_ioctl;
static long a = 100;

static struct cdevsw echo_cdevsw = {
	.d_version = D_VERSION,
	.d_open = echo_open,
	.d_close = echo_close,
	.d_read = echo_read,
	.d_write = echo_write,
	.d_ioctl = echo_ioctl,
	.d_name = "echo"
};

typedef struct echo {
	char buffer[BUFFER_SIZE + 1];
	int length;
} echo_t;

typedef struct nvecho {
	void *buf;
	size_t len;
} nvecho_t;

#define ECHO_IOCTL _IOWR('H', 1, nvecho_t)

static echo_t *message;
static struct cdev *dev;
static nvlist_t *nvl = NULL;
static struct sysctl_ctx_list clist;
static struct sysctl_oid *poid;

static int
sysctl_pointless_procedure(SYSCTL_HANDLER_ARGS) {
	char *buf = "Not at all. They could be carried.";
	return (sysctl_handle_string(oidp, buf, strlen(buf), req));
}

static int
echo_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return 0;
}

static int
echo_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return 0;
}

static int
echo_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error;
	size_t amt;

	amt = MIN(uio->uio_resid, BUFFER_SIZE);
	error = uiomove(message->buffer, amt, uio);
	if (error != 0) {
		uprintf("Write failed.\n");
		return error;
	}
	message->length = amt;
	message->buffer[amt] = '\0';
	return error;
}

static int
echo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error;
	int amount;

	amount = MIN(uio->uio_resid,
		(message->length - uio->uio_offset > 0) ?
		message->length - uio->uio_offset : 0);
	error = uiomove(message->buffer + uio->uio_offset, amount, uio);
	if (error != 0)
		uprintf("Read failed.\n");
	return (error);
}

static int
echo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td) {
	int error = 0;
	nvecho_t *udata = (nvecho_t *)data;
	nvecho_t kdata;

	switch (cmd) {
		case ECHO_IOCTL:
			if (udata->buf == NULL) {
				if (nvl == NULL) {
					udata->len = 0;
					return ENOMEM;
				}
				udata->len = nvlist_size(nvl);
			} else if (udata->len == 0) {
				if (nvl == NULL) {
					return ENOMEM;
				}
				kdata.len = nvlist_size(nvl);
				kdata.buf = nvlist_pack(nvl, &kdata.len);
				error = copyout(kdata.buf, udata->buf, kdata.len);
				if (error) {
					return error;
				}
				udata->len = kdata.len;
			} else {
				kdata.len = udata->len;
				kdata.buf = malloc(kdata.len, M_TEMP, M_WAITOK);
				error = copyin(udata->buf, kdata.buf, kdata.len);
				if (error) {
					free(kdata.buf, M_TEMP);
					return error;
				}
				if (nvl != NULL) {
					nvlist_destroy(nvl);
					nvl = NULL;
				}
				nvl = nvlist_unpack(kdata.buf, kdata.len, 0);
				free(kdata.buf, M_TEMP);
			}
			break;
		default:
			error = ENOTTY;
			break;
	}
	return error;
}

static int
modevent(module_t mod __unused, int event, void *arg __unused)
{
	int error = 0;

	switch (event) {
		case MOD_LOAD:
			uprintf("Hello, world!\n");
			sysctl_ctx_init(&clist);
			message = malloc(sizeof(echo_t), M_TEMP, M_WAITOK);
			dev = make_dev(&echo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "echo");
			poid = SYSCTL_ADD_NODE(
				&clist,
				SYSCTL_STATIC_CHILDREN(_hw),
				OID_AUTO,
				"echo",
				CTLFLAG_RW,
				0,
				"new top-level tree"
			);
			if (poid == NULL) {
				uprintf("SYSCTL_ADD_NODE failed.\n");
				return (EINVAL);
			}
			SYSCTL_ADD_LONG(
				&clist,
				SYSCTL_CHILDREN(poid),
				OID_AUTO,
				"long",
				CTLFLAG_RW,
				&a,
				"new long leaf"
			);
			SYSCTL_ADD_PROC(
				&clist,
				SYSCTL_CHILDREN(poid),
				OID_AUTO,
				"proc",
				CTLTYPE_STRING | CTLFLAG_RD,
				0,
				0,
				sysctl_pointless_procedure,
				"A",
				"new proc leaf"
			);
			break;
		case MOD_UNLOAD:
			if (sysctl_ctx_free(&clist)) {
				uprintf("sysctl_ctx_free failed.\n");
				return (ENOTEMPTY);
			}
			destroy_dev(dev);
			free(message, M_TEMP);
			nvlist_destroy(nvl);
			uprintf("Good bye, world!\n");
			break;
		default:
			error = EOPNOTSUPP;
			break;
	}
	return error;
}

DEV_MODULE(echo, modevent, NULL);
