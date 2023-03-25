#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/nv.h>
#include <sys/uio.h>
#include <sys/ioccom.h>

#define BUFFER_SIZE 256
MALLOC_DECLARE(M_ECHOBUF);
MALLOC_DEFINE(M_ECHOBUF, "echobuffer", "buffer for echo module");

static d_open_t echo_open;
static d_close_t echo_close;
static d_ioctl_t echo_ioctl;
static struct cdevsw echo_cdevsw = {
	.d_version = D_VERSION,
	.d_open = echo_open,
	.d_close = echo_close,
	.d_ioctl = echo_ioctl,
	.d_name = "echo"
};
typedef struct nvecho {
	void *buf;
	size_t len;
} nvecho_t;

#define ECHO_IOCTL _IOWR('H', 1, nvecho_t)

static struct cdev *dev = NULL;
static nvlist_t *nvl = NULL;
static struct sysctl_ctx_list clist = {0};

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
				kdata.buf = malloc(kdata.len, M_ECHOBUF, M_WAITOK);
				error = copyin(udata->buf, kdata.buf, kdata.len);
				if (error) {
					free(kdata.buf, M_ECHOBUF);
					return error;
				}
				if (nvl != NULL) {
					nvlist_destroy(nvl);
					nvl = NULL;
				}
				nvl = nvlist_unpack(kdata.buf, kdata.len, 0);
				free(kdata.buf, M_ECHOBUF);
			}
			break;
		default:
			error = ENOTTY;
			break;
	}
	return error;
}

static int
echo_sysctl(SYSCTL_HANDLER_ARGS) {
	nvecho_t kdata = {0};

	if (req->newptr) {
		kdata.len = req->newlen;
		kdata.buf = malloc(kdata.len, M_ECHOBUF, M_WAITOK);
		SYSCTL_IN(req, kdata.buf, kdata.len);
		if (nvl != NULL) {
			nvlist_destroy(nvl);
		}
		nvl = nvlist_unpack(kdata.buf, kdata.len, 0);
		if (nvl == NULL) {
			uprintf("Could not unpack nvlist!\n");
			free(kdata.buf, M_ECHOBUF);
			return EINVAL;
		}
		free(kdata.buf, M_ECHOBUF);
	}
	if (req->oldptr || req->oldlen) {
		if (nvl == NULL) {
			uprintf("No configuration set!\n");
			return ENOMEM;
		}
		kdata.buf = nvlist_pack(nvl, &kdata.len);
		if (kdata.buf == NULL) {
			uprintf("Error packing data\n");
			return EINVAL;
		}
		SYSCTL_OUT(req, kdata.buf, kdata.len);
		free(kdata.buf, M_ECHOBUF);
	}
	return 0;
}

static int
modevent(module_t mod __unused, int event, void *arg __unused)
{
	int error = 0;
	struct sysctl_oid *poid;

	switch (event) {
		case MOD_LOAD:
			dev = make_dev(&echo_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666, "echo");
			sysctl_ctx_init(&clist);
			poid = SYSCTL_ADD_NODE(
				&clist,
				SYSCTL_STATIC_CHILDREN(_kern),
				OID_AUTO,
				"echo",
				CTLFLAG_RW,
				0,
				"new tree"
			);
			if (poid == NULL) {
				uprintf("SYSCTL_ADD_NODE failed.\n");
				return EINVAL;
			}
			SYSCTL_ADD_PROC(
				&clist,
				SYSCTL_CHILDREN(poid),
				OID_AUTO,
				"config",
				CTLTYPE_OPAQUE | CTLFLAG_RW,
				NULL,
				0,
				echo_sysctl,
				"S,nvecho",
				"Configure using nvlist"
			);
			break;
		case MOD_UNLOAD:
			if (sysctl_ctx_free(&clist)) {
				uprintf("sysctl_ctx_free failed.\n");
				return ENOTEMPTY;
			}
			destroy_dev(dev);
			nvlist_destroy(nvl);
			break;
		default:
			error = EOPNOTSUPP;
			break;
	}
	return error;
}

DEV_MODULE(echo, modevent, NULL);
