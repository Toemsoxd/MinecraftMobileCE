#include "renderer.h"
#include <math.h>
struct Vertex{float x,y,z;DWORD color;};
#define FVF_VERTEX (D3DFVF_XYZ|D3DFVF_DIFFUSE)
static D3DMATRIX Identity(){D3DMATRIX m;for(int r=0;r<4;++r)for(int c=0;c<4;++c)m.m[r][c]=(r==c)?1.0f:0.0f;return m;}
static D3DMATRIX Mul(const D3DMATRIX&a,const D3DMATRIX&b){D3DMATRIX o;for(int r=0;r<4;++r)for(int c=0;c<4;++c){o.m[r][c]=0;for(int k=0;k<4;++k)o.m[r][c]+=a.m[r][k]*b.m[k][c];}return o;}
static D3DMATRIX RotY(float a){D3DMATRIX m=Identity();float s=(float)sin(a),c=(float)cos(a);m._11=c;m._13=s;m._31=-s;m._33=c;return m;}
static D3DMATRIX RotX(float a){D3DMATRIX m=Identity();float s=(float)sin(a),c=(float)cos(a);m._22=c;m._23=-s;m._32=s;m._33=c;return m;}
static D3DMATRIX Trans(float x,float y,float z){D3DMATRIX m=Identity();m._41=x;m._42=y;m._43=z;return m;}
static D3DMATRIX Perspective(float f,float aspect,float zn,float zf){D3DMATRIX m;for(int r=0;r<4;++r)for(int c=0;c<4;++c)m.m[r][c]=0;float ys=1.0f/(float)tan(f*.5f),xs=ys/aspect;m._11=xs;m._22=ys;m._33=zf/(zf-zn);m._34=1;m._43=-zn*zf/(zf-zn);return m;}
static void Quad(IDirect3DDevice8*d,const Vertex*v){d->DrawPrimitiveUP(D3DPT_TRIANGLEFAN,2,v,sizeof(Vertex));}
static void Top(IDirect3DDevice8*d,float x,float y,float z,DWORD c){Vertex v[4]={{x,y,z,c},{x+1,y,z,c},{x+1,y,z+1,c},{x,y,z+1,c}};Quad(d,v);}
static void Side(IDirect3DDevice8*d,float x1,float y1,float z1,float x2,float y2,float z2,DWORD c){Vertex v[4]={{x1,y1,z1,c},{x2,y1,z2,c},{x2,y2,z2,c},{x1,y2,z1,c}};Quad(d,v);}
Renderer::Renderer():m_d3d(0),m_device(0),m_hwnd(0),m_width(240),m_height(320){}
Renderer::~Renderer(){Shutdown();}
bool Renderer::Initialize(HWND hwnd,int width,int height){
    D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));m_hwnd=hwnd;m_width=width;m_height=height;
    m_d3d=Direct3DCreate8(D3D_SDK_VERSION);if(!m_d3d)return false;
    pp.Windowed=FALSE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_R5G6B5;
    pp.BackBufferWidth=width;pp.BackBufferHeight=height;pp.BackBufferCount=1;pp.hDeviceWindow=hwnd;
    pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D16;pp.FullScreen_PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr=m_d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_DEFAULT,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&m_device);
    if(FAILED(hr)){m_d3d->Release();m_d3d=0;return false;}
    m_device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);m_device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    m_device->SetRenderState(D3DRS_LIGHTING,FALSE);m_device->SetVertexShader(FVF_VERTEX);return true;
}
void Renderer::Shutdown(){if(m_device){m_device->Release();m_device=0;}if(m_d3d){m_d3d->Release();m_d3d=0;}}
void Renderer::SetupMatrices(const Camera&c){
    D3DMATRIX p=Perspective(70.0f*3.14159265f/180.0f,(float)m_width/(float)m_height,.1f,256.0f);
    D3DMATRIX r=Mul(RotX(-c.pitch),RotY(-c.yaw)),v=Mul(Trans(-c.x,-c.y,-c.z),r);
    m_device->SetTransform(D3DTS_PROJECTION,&p);m_device->SetTransform(D3DTS_VIEW,&v);
}
void Renderer::DrawTerrain(const Terrain&t,const Camera&c){
    int cx=(int)c.x,cz=(int)c.z,radius=40;
    for(int z=cz-radius;z<=cz+radius;++z){if(z<0||z>=WORLD_SIZE)continue;
        for(int x=cx-radius;x<=cx+radius;++x){if(x<0||x>=WORLD_SIZE)continue;
            int h=t.GetHeight(x,z),l=t.GetHeight(x-1,z),rr=t.GetHeight(x+1,z),f=t.GetHeight(x,z-1),b=t.GetHeight(x,z+1);
            Top(m_device,(float)x,(float)h,(float)z,D3DCOLOR_XRGB(80,170,70));
            if(l<h)Side(m_device,(float)x,(float)l,(float)z,(float)x,(float)h,(float)z+1,D3DCOLOR_XRGB(95,70,45));
            if(rr<h)Side(m_device,(float)x+1,(float)rr,(float)z,(float)x+1,(float)h,(float)z+1,D3DCOLOR_XRGB(90,65,42));
            if(f<h)Side(m_device,(float)x,(float)f,(float)z,(float)x+1,(float)h,(float)z,D3DCOLOR_XRGB(85,62,40));
            if(b<h)Side(m_device,(float)x,(float)b,(float)z+1,(float)x+1,(float)h,(float)z+1,D3DCOLOR_XRGB(75,55,38));
        }
    }
}
void Renderer::Render(const Terrain&t,const Camera&c){
    if(!m_device)return;SetupMatrices(c);
    m_device->Clear(0,0,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,D3DCOLOR_XRGB(115,185,235),1.0f,0);
    if(SUCCEEDED(m_device->BeginScene())){DrawTerrain(t,c);m_device->EndScene();}m_device->Present(0,0,0,0);
}
