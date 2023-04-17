#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>
#include <cairo.h>
#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <termios.h>
#include <cairo.h>

#define SHIFT 10

struct data
{   
    bool updateFlag;
    bool left;
    bool right;
    bool up;
    bool down;
};

struct data object;

uint64_t get_property_value(int drm_fd,uint32_t object_id,uint32_t object_type,const char *prop_name)
{
    drmModeObjectProperties *props = drmModeObjectGetProperties(drm_fd,object_id,object_type);
    for(uint32_t i = 0; i<props->count_props; i++)
    {
        drmModePropertyRes *prop = drmModeGetProperty(drm_fd,props->props[i]);
        uint64_t val = props->prop_values[i];
        if(strcmp(prop->name, prop_name)==0)
        {
            drmModeFreeProperty(prop);
            drmModeFreeObjectProperties(props);
            return val;
        }
        drmModeFreeProperty(prop);
    }
    abort();  //Property not found
}

void *location(void *vargp)
{
    struct termios info;
    tcgetattr(0, &info);     /* get current terminal attirbutes; 0 is the file descriptor for stdin */
    info.c_lflag &= ~ICANON; /* disable canonical mode */
    info.c_cc[VMIN] = 1;     /* wait until at least one keystroke available */
    info.c_cc[VTIME] = 0;    /* no timeout */
    tcsetattr(0, TCSANOW, &info);
    int ch;
    bool a1Set =false , a2Set =false ,a3Set =false;
    int a1,a2,a3 ;

    while ((ch = getchar()) != 100)
    {
		if (!a1Set)
		{
			a1Set = true;
			a1 = ch;
		}
		else if (!a2Set)
		{
			a2Set = true;
			a2 = ch;
		}
		else if (!a3Set)
		{
			a3Set = true;
			a3 = ch;
		}
		// printf("\nThis is%d\n" , a1Set && a2Set && a3Set);
		if (a1Set && a2Set && a3Set)
		{
			if (a1 == 27 && a2 == 91)
			{
				switch (a3)
				{
				case 68:
					// printf("Left print");
					object.left = true;
                    printf("left\n");
					break;
				case 67:
					// printf("\nRight print\n");
					object.right = true;
                    printf("right\n");
					break;
				case 66:
					// printf("down print");
					object.down = true;
                    printf("down\n");
					break;
				case 65:
					object.up = true;
					// printf("Up print");
                    printf("up\n");
					break;
				default:
					break;
				}
			}
			a1Set = a2Set = a3Set = false;
		}
		
        object.updateFlag = true;
    }

}


void *findinghw(void *vargp)
{
    printf("1.4\n");
    int drm_fd = open("/dev/dri/card0",O_RDWR|O_NONBLOCK);
    if(drm_fd<0)
    {
        perror("open failed");
        return 1;
    }
    if(drmSetClientCap(drm_fd,DRM_CLIENT_CAP_UNIVERSAL_PLANES,1)!=0)
    {   perror("drmSetClientCap(UNIVERSAL_PLANES) failed");
        return 1;   
    }
    if(drmSetClientCap(drm_fd,DRM_CLIENT_CAP_ATOMIC,1)!=0)
    {   perror("drmSetClientCap(ATOMIC) failed");
        return 1;   
    }
    drmModeRes *resources = drmModeGetResources(drm_fd);
    drmModeCrtc *crtc;

    //.............Get the first Crtc lighted up..............

    for(int i = 0;i<resources->count_crtcs;i++)
    {
        uint32_t crtc_id = resources->crtcs[i];
        crtc = drmModeGetCrtc(drm_fd,crtc_id);
        if(crtc->mode_valid)
        {
            printf("Mode found\n");
            break;
        }
        
        drmModeFreeCrtc(crtc);
        crtc = NULL;
    }
    
    assert(crtc!=NULL);
    printf("Using crtc %u\n",crtc->crtc_id);
    drmModeModeInfo mode = crtc->mode;
    printf("Using mode %d * %d %dHz\n",mode.hdisplay,mode.vdisplay,mode.vrefresh);


    drmModePlaneRes *planes = drmModeGetPlaneResources(drm_fd);
    drmModePlane *pplane;
    printf("\nplane count %d\n", planes->count_planes);
    for(uint32_t i = 0;i<planes->count_planes;i++)
    {   
        uint32_t pplane_id = planes->planes[i];
        printf("plane id %d\n", pplane_id);

        pplane = drmModeGetPlane(drm_fd,pplane_id);
        uint64_t pplane_type = get_property_value(drm_fd,pplane_id,DRM_MODE_OBJECT_PLANE,"type");
        printf("plane type %ld\n", pplane_type);

        printf("crtc_id %d\n",crtc->crtc_id);
        printf("crtc_id %d\n",pplane->crtc_id);
        printf("possible crtc is %d\n",pplane->possible_crtcs);
        printf("pptype is %d\n\n",pplane_type);
        // pplane->crtc_id = 45;

        if(pplane->crtc_id == crtc->crtc_id && pplane_type == DRM_PLANE_TYPE_PRIMARY)
            break;

        drmModeFreePlane(pplane);
        pplane = NULL;
    }
    assert(pplane!=NULL);
    // printf("1.1\n");
    printf("Using the primary plane %u\n",pplane->plane_id);

   //.............getting the fb...........
    int width = mode.hdisplay;
    int height = mode.vdisplay;

    struct drm_mode_create_dumb create ={
        .width = width,
        .height = height,
        .bpp = 32,
    };

    drmIoctl(drm_fd,DRM_IOCTL_MODE_CREATE_DUMB,&create);
    uint32_t handle = create.handle;
    uint32_t stride = create.pitch;
    uint32_t size = create.size;
    
    //..............Create a DRM Framebuffer object............

    uint32_t handles[4] = {handle};
    uint32_t strides[4] = {stride};
    uint32_t offsets[4] = { 0 };
    uint32_t fb_id = 0;

    drmModeAddFB2(drm_fd,width,height,DRM_FORMAT_XRGB8888,handles,strides,offsets,&fb_id,0);
    printf("Allocates buffer %u\n",fb_id);

    //............Create a Memory mapping...................

    struct drm_mode_map_dumb map = {.handle = handle};
    drmIoctl(drm_fd,DRM_IOCTL_MODE_MAP_DUMB, &map);
    uint8_t *data = mmap(0,size, PROT_READ|PROT_WRITE,MAP_SHARED,drm_fd, map.offset);
    
    printf("1.5\n");
    uint8_t color[] = { 0xFF, 0xFF, 0xFF, 0xFF };

    for(int y=0;y<height;y++)
        {
            for(int x=0;x<width;x++)
            {
                size_t offset = y*stride + x*sizeof(color);
                memcpy(&data[offset],color,sizeof(color));
            }
        }
    
    printf("white color\n");

    cairo_t* cr;
	cairo_surface_t *surface,*image1,*image2,*image3,*image4,*image5,*image6,*image7,*image8,*image9,*image10,*image11,*image12,*image13,*image14,*image15,*image16,*image17,*image18,*image19,*image20,*image21,*image22,*image23,*image24,*image25;
    
    image1 = cairo_image_surface_create_from_png("pic1.png");
    image2 = cairo_image_surface_create_from_png("pic2.png");
    image3 = cairo_image_surface_create_from_png("pic3.png");
    image4 = cairo_image_surface_create_from_png("pic4.png");
    image5 = cairo_image_surface_create_from_png("pic5.png");
    image6 = cairo_image_surface_create_from_png("pic6.png");
    image7 = cairo_image_surface_create_from_png("pic7.png");
    image8 = cairo_image_surface_create_from_png("pic8.png");
    image9 = cairo_image_surface_create_from_png("pic9.png");
    image10 = cairo_image_surface_create_from_png("pic10.png");
    image11 = cairo_image_surface_create_from_png("pic11.png");
    image12 = cairo_image_surface_create_from_png("pic12.png");
    image13 = cairo_image_surface_create_from_png("Lpic1.png");
    image14 = cairo_image_surface_create_from_png("Lpic2.png");
    image15 = cairo_image_surface_create_from_png("Lpic3.png");
    image16 = cairo_image_surface_create_from_png("Lpic4.png");
    image17 = cairo_image_surface_create_from_png("Lpic5.png");
    image18 = cairo_image_surface_create_from_png("Lpic6.png");
    image19 = cairo_image_surface_create_from_png("Lpic7.png");
    image20 = cairo_image_surface_create_from_png("Lpic8.png");
    image21 = cairo_image_surface_create_from_png("Lpic9.png");
    image22 = cairo_image_surface_create_from_png("Lpic10.png");
    image23 = cairo_image_surface_create_from_png("Lpic11.png");
    image24 = cairo_image_surface_create_from_png("Lpic12.png");
    image25 = cairo_image_surface_create_from_png("bg.PNG");
    surface = cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32,width,height,stride);
    cr = cairo_create (surface);
   

    cairo_set_source_surface(cr,image25,0,0);
    cairo_paint(cr);

    printf("\n1st image\n");
    int i=0;
    int j=0;
    int k=0;
    int l=0;
    int refs =0;
    int refs1 =660;
    int shift=5;
    while (1)
	{

		printf("\nIn while loop\n");
        printf("%d",object.updateFlag);
		if (object.updateFlag)
		{
            printf("\nLeft pressed\n");
			object.updateFlag = false;
            // printf("\n\nleft\n\n");
			// int yG = (object.y > 0 ? object.y : 0);
			// int xG = (object.x > 0 ? object.x : 0);

			// if(object.left){
			// 	object.left = false;
			// 	// fillBuffer(data ,object.oldX ,  object.oldX + MOVMENT ,yG ,((height * 0.1) + yG) ,stride , thirdColor , N_ELEMENTS(thirdColor) );
			// }
            if (object.left)
			{

                printf("while left\n");
				object.left=  false;
                // surface = cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32,width,height,stride);
	            // cr = cairo_create (surface);
                switch (j)
                {
                case 0:
                    cairo_set_source_surface(cr, image13, refs-5, refs1);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    cairo_paint(cr);
                    break;

                case 1:
                    cairo_set_source_surface(cr, image14, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;
                
                case 2:
                    cairo_set_source_surface(cr, image15,refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 3:
                    cairo_set_source_surface(cr, image16, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 4:
                    cairo_set_source_surface(cr, image17, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 5:
                    cairo_set_source_surface(cr, image18, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 6:
                    cairo_set_source_surface(cr, image19, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 7:
                    cairo_set_source_surface(cr, image20, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 8:
                    cairo_set_source_surface(cr, image21, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 9:
                    cairo_set_source_surface(cr, image22, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 10:
                    cairo_set_source_surface(cr, image23, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j++;
                    refs -= 5;
                    shift+=5;
                    break;

                case 11:
                    cairo_set_source_surface(cr, image24, refs-5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    refs -= 5;
                    shift+=5;
                    break;

                default:
                    break;
                }
            }
			if (object.right)
			{

                printf("while right\n");
				   object.right=  false;
                // surface = cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32,width,height,stride);
	            // cr = cairo_create (surface);
                switch (i)
                {
                case 0:
                    cairo_set_source_surface(cr, image2, refs+5, refs1);
                    i++;
                    j=0;
                    shift+=5;
                    refs += 5;
                    printf("j is %d\n",j);
                    cairo_paint(cr);
                    break;

                case 1:
                    cairo_set_source_surface(cr, image3, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;
                
                case 2:
                    cairo_set_source_surface(cr, image4, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    shift+=5;
                    refs += 5;
                    break;

                case 3:
                    cairo_set_source_surface(cr, image5, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                case 4:
                    cairo_set_source_surface(cr, image6, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                case 5:
                    cairo_set_source_surface(cr, image7, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                case 6:
                    cairo_set_source_surface(cr, image8, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                case 7:
                    cairo_set_source_surface(cr, image9, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                case 8:
                    cairo_set_source_surface(cr, image10, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                case 9:
                    cairo_set_source_surface(cr, image11, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                case 10:
                    cairo_set_source_surface(cr, image12, refs+5, refs1);
                    cairo_paint(cr);
                    i++;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                case 11:
                    cairo_set_source_surface(cr, image1, refs+5, refs1);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    refs += 5;
                    shift+=5;
                    break;

                default:
                    break;
                }
            }

            if (object.up)
			{

                printf("while up\n");
				   object.up=  false;
                // surface = cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32,width,height,stride);
	            // cr = cairo_create (surface);
                switch (k)
                {
                case 0:
                    cairo_set_source_surface(cr, image2, refs, refs1-5);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    cairo_paint(cr);
                    break;

                case 1:
                    cairo_set_source_surface(cr, image3, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;
                
                case 2:
                    cairo_set_source_surface(cr, image4,refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 3:
                    cairo_set_source_surface(cr, image5, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 4:
                    cairo_set_source_surface(cr, image6, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 5:
                    cairo_set_source_surface(cr, image7, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 6:
                    cairo_set_source_surface(cr, image8, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 7:
                    cairo_set_source_surface(cr, image9, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 8:
                    cairo_set_source_surface(cr, image10, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 9:
                    cairo_set_source_surface(cr, image11, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 10:
                    cairo_set_source_surface(cr, image12, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k++;
                    refs1 -= 5;
                    shift+=5;
                    break;

                case 11:
                    cairo_set_source_surface(cr, image1, refs, refs1-5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    refs1 -= 5;
                    shift+=5;
                    break;

                default:
                    break;
                }
            }

            if (object.down)
			{

                printf("while down\n");
				   object.down=  false;
                // surface = cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32,width,height,stride);
	            // cr = cairo_create (surface);
                switch (l)
                {
                case 0:
                    cairo_set_source_surface(cr, image2, refs, refs1+5);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    cairo_paint(cr);
                    break;

                case 1:
                    cairo_set_source_surface(cr, image3, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;
                
                case 2:
                    cairo_set_source_surface(cr, image4,refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 3:
                    cairo_set_source_surface(cr, image5, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 4:
                    cairo_set_source_surface(cr, image6, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 5:
                    cairo_set_source_surface(cr, image7, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 6:
                    cairo_set_source_surface(cr, image8, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 7:
                    cairo_set_source_surface(cr, image9, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 8:
                    cairo_set_source_surface(cr, image10, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 9:
                    cairo_set_source_surface(cr, image11, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 10:
                    cairo_set_source_surface(cr, image12, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l++;
                    refs1 += 5;
                    shift+=5;
                    break;

                case 11:
                    cairo_set_source_surface(cr, image1, refs, refs1+5);
                    cairo_paint(cr);
                    i=0;
                    j=0;
                    k=0;
                    l=0;
                    refs1 += 5;
                    shift+=5;
                    break;

                default:
                    break;
                }
            }
			
		}
        
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    printf("req %d\n",req);
    uint32_t plane_id = pplane->plane_id;

    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"FB_ID",fb_id);
    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"SRC_X",0);
    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"SRC_Y",0);
    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"SRC_W",width<<16);
    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"SRC_H",height<<16);
    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_X",0);
    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_Y",0);
    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_W",width);
    add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_H",height);
    // add_property(drm_fd,req,plane_id,DRM_MODE_OBJECT_PLANE,"CRTC_ID",45);

    uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK;
    int ret = drmModeAtomicCommit(drm_fd,req,flags,NULL);
    printf("ret is %d\n",ret);
    if(ret)
    {
        perror("drmModeAtomicCommit failed\n");
        // return 1;
    }

    //Sleep for a while so that we can see the results on screen
    // sleep(10);
    struct timespec ts = {.tv_nsec = 16666667};

    nanosleep(&ts, NULL);
    // return 0;
    }
}

void add_property(int drm_fd, drmModeAtomicReq *req,uint object_id,uint object_type,const char *prop_name,uint value)
{
    uint32_t prop_id = 0;
    drmModeObjectProperties *props = drmModeObjectGetProperties(drm_fd,object_id,object_type);
    for(uint32_t i = 0;i<props->count_props;i++)
    {
        drmModePropertyRes *prop = drmModeGetProperty(drm_fd,props->props[i]);
        if(strcmp(prop->name,prop_name)==0)
        {
            prop_id = prop->prop_id;
            break;
        }
    }
    assert(prop_id !=0);
    drmModeAtomicAddProperty(req,object_id,prop_id,value);
}


int main(int argc, char *argv[]) 
{         
    printf("1.1\n");
    pthread_t thread_id;
    printf("1.2\n");
    pthread_create(&thread_id, NULL, location, NULL);
    pthread_create(&thread_id, NULL, findinghw, NULL);
    printf("After thread\n");
	pthread_exit(NULL);
	// return 0;
}
