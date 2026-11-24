#include "media/media_locations.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assign(VtMediaLocation *e,const char *path,unsigned mask){
    memset(e,0,sizeof(*e));
    snprintf(e->path,sizeof(e->path),"%s",path);
    e->media_mask=mask;
}
int main(void){
    VtMediaLocations l,readback;
    assert(vt_media_locations_load(&l)==0);
    assert(l.count==12);
    assert(vt_media_locations_validate(&l)==0);
    char norm[512];
    assert(vt_media_location_normalize("UX0://Movies///SciFi/",norm,sizeof(norm))==0);
    assert(strcmp(norm,"ux0:Movies/SciFi")==0);
    assert(vt_media_location_normalize("uma0:../danger",norm,sizeof(norm))<0);
    assert(vt_media_location_normalize("ux0:folder/./relative",norm,sizeof(norm))<0);
    assert(vt_media_location_normalize("not/absolute",norm,sizeof(norm))<0);
    memset(&l,0,sizeof(l));
    l.count=2;
    assign(&l.locations[0],"ux0:Media",VT_MEDIA_LOC_VIDEO);
    assign(&l.locations[1],"ux0:Media/Albums",VT_MEDIA_LOC_AUDIO);
    assert(vt_media_locations_validate(&l)==0); /* distinct categories */
    uint32_t h=vt_media_locations_hash(&l);
    assert(vt_media_locations_save(&l)==0);
    memset(&readback,0,sizeof(readback));
    assert(vt_media_locations_load(&readback)==0);
    assert(readback.count==2);
    assert(vt_media_locations_hash(&readback)==h);
    assert(strcmp(readback.locations[1].path,"ux0:Media/Albums")==0);
    assign(&l.locations[1],"ux0:Media/Films",VT_MEDIA_LOC_VIDEO);
    assert(vt_media_locations_validate(&l)==-2); /* video overlaps video */
    assert(vt_media_locations_save(&l)<0);
    /* Reject a corrupt fixed-width, non-NUL-terminated path safely. */
    l.count=1;
    memset(l.locations[0].path, 'x', sizeof(l.locations[0].path));
    l.locations[0].media_mask=VT_MEDIA_LOC_VIDEO;
    assert(vt_media_locations_validate(&l)==-1);
    assign(&l.locations[0],"ux0:Safe",VT_MEDIA_LOC_VIDEO);
    l.count=0;
    assert(vt_media_locations_save(&l)==0);
    assert(vt_media_locations_load(&readback)==0);
    assert(readback.count==0); /* deleted defaults must NOT reappear */
    puts("PASS: defaults, path normalization, independent category folders, overlap guard, save/reload, zero-folders persistence, corrupt path rejection");
    return 0;
}
