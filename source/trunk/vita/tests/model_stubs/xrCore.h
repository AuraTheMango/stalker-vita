
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cassert>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using s8=int8_t;using s16=int16_t;
#define VERIFY(x) assert(x)
#define _abs std::abs
#define _sin std::sin
#define _cos std::cos
constexpr float flt_zero=1e-6f;
struct Fvector {
 float x,y,z;
 Fvector& set(float a,float b,float c){x=a;y=b;z=c;return *this;}
 Fvector& set(const Fvector& v){*this=v;return *this;}
 Fvector& sub(const Fvector&a,const Fvector&b){return set(a.x-b.x,a.y-b.y,a.z-b.z);}
 float square_magnitude()const{return x*x+y*y+z*z;}
 Fvector& normalize(){float n=std::sqrt(square_magnitude());x/=n;y/=n;z/=n;return *this;}
 Fvector& crossproduct(const Fvector&a,const Fvector&b){return set(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
 Fvector& lerp(const Fvector&a,const Fvector&b,float t){return set(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t);}
};
struct Fquaternion {float x,y,z,w;};
struct Fmatrix {
 using T=float;using Self=Fmatrix;using SelfRef=Fmatrix&;using Tvector=Fvector;
 union {
 struct{float _11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44;};
 struct{Fvector i;float _14_;Fvector j;float _24_;Fvector k;float _34_;Fvector c;float _44_;};
 float m[4][4];};
Fmatrix& set(const Fvector& R,const Fvector& N,const Fvector& D,const Fvector& C){
		i.set(R); _14_=0;
		j.set(N); _24_=0;
		k.set(D); _34_=0;
		c.set(C); _44_=1;
		return *this;
	}
Fmatrix& mul_43(const Fmatrix& A,const Fmatrix& B){
		VERIFY	((this!=&A)&&(this!=&B));
		m[0][0] = A.m[0][0] * B.m[0][0] + A.m[1][0] * B.m[0][1] + A.m[2][0] * B.m[0][2];
		m[0][1] = A.m[0][1] * B.m[0][0] + A.m[1][1] * B.m[0][1] + A.m[2][1] * B.m[0][2];
		m[0][2] = A.m[0][2] * B.m[0][0] + A.m[1][2] * B.m[0][1] + A.m[2][2] * B.m[0][2];
		m[0][3] = 0;

		m[1][0] = A.m[0][0] * B.m[1][0] + A.m[1][0] * B.m[1][1] + A.m[2][0] * B.m[1][2];
		m[1][1] = A.m[0][1] * B.m[1][0] + A.m[1][1] * B.m[1][1] + A.m[2][1] * B.m[1][2];
		m[1][2] = A.m[0][2] * B.m[1][0] + A.m[1][2] * B.m[1][1] + A.m[2][2] * B.m[1][2];
		m[1][3] = 0;

		m[2][0] = A.m[0][0] * B.m[2][0] + A.m[1][0] * B.m[2][1] + A.m[2][0] * B.m[2][2];
		m[2][1] = A.m[0][1] * B.m[2][0] + A.m[1][1] * B.m[2][1] + A.m[2][1] * B.m[2][2];
		m[2][2] = A.m[0][2] * B.m[2][0] + A.m[1][2] * B.m[2][1] + A.m[2][2] * B.m[2][2];
		m[2][3] = 0;

		m[3][0] = A.m[0][0] * B.m[3][0] + A.m[1][0] * B.m[3][1] + A.m[2][0] * B.m[3][2] + A.m[3][0];
		m[3][1] = A.m[0][1] * B.m[3][0] + A.m[1][1] * B.m[3][1] + A.m[2][1] * B.m[3][2] + A.m[3][1];
		m[3][2] = A.m[0][2] * B.m[3][0] + A.m[1][2] * B.m[3][1] + A.m[2][2] * B.m[3][2] + A.m[3][2];
		m[3][3] = 1;
		return *this;
	}
Fmatrix& invert(const Fmatrix& a){		// important: this is 4x3 invert, not the 4x4 one
		// faster than self-invert
		T fDetInv = ( a._11 * ( a._22 * a._33 - a._23 * a._32 ) -
			a._12 * ( a._21 * a._33 - a._23 * a._31 ) +
			a._13 * ( a._21 * a._32 - a._22 * a._31 ) );

		VERIFY(_abs(fDetInv)>flt_zero);
		fDetInv=1.0f/fDetInv;

		_11 =  fDetInv * ( a._22 * a._33 - a._23 * a._32 );
		_12 = -fDetInv * ( a._12 * a._33 - a._13 * a._32 );
		_13 =  fDetInv * ( a._12 * a._23 - a._13 * a._22 );
		_14 = 0.0f;

		_21 = -fDetInv * ( a._21 * a._33 - a._23 * a._31 );
		_22 =  fDetInv * ( a._11 * a._33 - a._13 * a._31 );
		_23 = -fDetInv * ( a._11 * a._23 - a._13 * a._21 );
		_24 = 0.0f;

		_31 =  fDetInv * ( a._21 * a._32 - a._22 * a._31 );
		_32 = -fDetInv * ( a._11 * a._32 - a._12 * a._31 );
		_33 =  fDetInv * ( a._11 * a._22 - a._12 * a._21 );
		_34 = 0.0f;

		_41 = -( a._41 * _11 + a._42 * _21 + a._43 * _31 );
		_42 = -( a._41 * _12 + a._42 * _22 + a._43 * _32 );
		_43 = -( a._41 * _13 + a._42 * _23 + a._43 * _33 );
		_44 = 1.0f;
		return *this;
	}
Fmatrix& setHPB(float h,float p,float b){
        T _ch, _cp, _cb, _sh, _sp, _sb, _cc, _cs, _sc, _ss;

        _sh = _sin(h); _ch = _cos(h);
        _sp = _sin(p); _cp = _cos(p);
        _sb = _sin(b); _cb = _cos(b);
        _cc = _ch*_cb; _cs = _ch*_sb; _sc = _sh*_cb; _ss = _sh*_sb;

        i.set(_cc-_sp*_ss,	-_cp*_sb,	_sp*_cs+_sc	);	_14_=0;
        j.set(_sp*_sc+_cs,	_cp*_cb, 	_ss-_sp*_cc	);	_24_=0;
        k.set(-_cp*_sh,    	_sp,		_cp*_ch		);	_34_=0;
        c.set(0,			0,			0			);  _44_=1;
		return *this; 
    }
void transform_tiny(Fvector& dest,const Fvector& v)const{
		dest.x = v.x*_11 + v.y*_21 + v.z*_31 + _41;
		dest.y = v.x*_12 + v.y*_22 + v.z*_32 + _42;
		dest.z = v.x*_13 + v.y*_23 + v.z*_33 + _43;
	}
Fmatrix& setXYZi(Fvector& v){return setHPB(-v.y,-v.x,-v.z);}
Fmatrix& mk_xform(const Fquaternion& Q,const Fvector& V){
	T xx = Q.x*Q.x; T yy = Q.y*Q.y; T zz = Q.z*Q.z;
	T xy = Q.x*Q.y; T xz = Q.x*Q.z; T yz = Q.y*Q.z;
	T wx = Q.w*Q.x; T wy = Q.w*Q.y; T wz = Q.w*Q.z;

	_11 = 1 - 2 * ( yy + zz );	_12 =     2 * ( xy - wz );	_13 =     2 * ( xz + wy );	_14 = 0;
	_21 =     2 * ( xy + wz );	_22 = 1 - 2 * ( xx + zz );	_23 =     2 * ( yz - wx );	_24 = 0;
	_31 =     2 * ( xz - wy );	_32 =     2 * ( yz + wx );	_33 = 1 - 2 * ( xx + yy );	_34 = 0;
	_41 = V.x;					_42 = V.y;					_43 = V.z;					_44 = 1;
	return *this;
}
};
