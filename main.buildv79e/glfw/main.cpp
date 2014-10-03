
#include "main.h"

//${CONFIG_BEGIN}
#define CFG_BINARY_FILES *.bin|*.dat
#define CFG_BRL_GAMETARGET_IMPLEMENTED 1
#define CFG_BRL_OS_IMPLEMENTED 1
#define CFG_BRL_THREAD_IMPLEMENTED 1
#define CFG_CONFIG debug
#define CFG_CPP_GC_MODE 1
#define CFG_GLFW_SWAP_INTERVAL -1
#define CFG_GLFW_USE_MINGW 0
#define CFG_GLFW_WINDOW_FULLSCREEN 0
#define CFG_GLFW_WINDOW_HEIGHT 480
#define CFG_GLFW_WINDOW_RESIZABLE 0
#define CFG_GLFW_WINDOW_TITLE Monkey Game
#define CFG_GLFW_WINDOW_WIDTH 640
#define CFG_HOST winnt
#define CFG_IMAGE_FILES *.png|*.jpg
#define CFG_LANG cpp
#define CFG_MOJO_AUTO_SUSPEND_ENABLED 1
#define CFG_MOJO_DRIVER_IMPLEMENTED 1
#define CFG_MOJO_IMAGE_FILTERING_ENABLED 0
#define CFG_MUSIC_FILES *.wav|*.ogg
#define CFG_OPENGL_DEPTH_BUFFER_ENABLED 0
#define CFG_OPENGL_GLES20_ENABLED 0
#define CFG_SAFEMODE 0
#define CFG_SOUND_FILES *.wav|*.ogg
#define CFG_TARGET glfw
#define CFG_TEXT_FILES *.txt|*.xml|*.json
//${CONFIG_END}

//${TRANSCODE_BEGIN}

#include <wctype.h>
#include <locale.h>

// C++ Monkey runtime.
//
// Placed into the public domain 24/02/2011.
// No warranty implied; use at your own risk.

//***** Monkey Types *****

typedef wchar_t Char;
template<class T> class Array;
class String;
class Object;

#if CFG_CPP_DOUBLE_PRECISION_FLOATS
typedef double Float;
#define FLOAT(X) X
#else
typedef float Float;
#define FLOAT(X) X##f
#endif

void dbg_error( const char *p );

#if !_MSC_VER
#define sprintf_s sprintf
#define sscanf_s sscanf
#endif

//***** GC Config *****

#define DEBUG_GC 0

// GC mode:
//
// 0 = disabled
// 1 = Incremental GC every OnWhatever
// 2 = Incremental GC every allocation
//
#ifndef CFG_CPP_GC_MODE
#define CFG_CPP_GC_MODE 1
#endif

//How many bytes alloced to trigger GC
//
#ifndef CFG_CPP_GC_TRIGGER
#define CFG_CPP_GC_TRIGGER 8*1024*1024
#endif

//GC_MODE 2 needs to track locals on a stack - this may need to be bumped if your app uses a LOT of locals, eg: is heavily recursive...
//
#ifndef CFG_CPP_GC_MAX_LOCALS
#define CFG_CPP_GC_MAX_LOCALS 8192
#endif

// ***** GC *****

#if _WIN32

int gc_micros(){
	static int f;
	static LARGE_INTEGER pcf;
	if( !f ){
		if( QueryPerformanceFrequency( &pcf ) && pcf.QuadPart>=1000000L ){
			pcf.QuadPart/=1000000L;
			f=1;
		}else{
			f=-1;
		}
	}
	if( f>0 ){
		LARGE_INTEGER pc;
		if( QueryPerformanceCounter( &pc ) ) return pc.QuadPart/pcf.QuadPart;
		f=-1;
	}
	return 0;// timeGetTime()*1000;
}

#elif __APPLE__

#include <mach/mach_time.h>

int gc_micros(){
	static int f;
	static mach_timebase_info_data_t timeInfo;
	if( !f ){
		mach_timebase_info( &timeInfo );
		timeInfo.denom*=1000L;
		f=1;
	}
	return mach_absolute_time()*timeInfo.numer/timeInfo.denom;
}

#else

int gc_micros(){
	return 0;
}

#endif

#define gc_mark_roots gc_mark

void gc_mark_roots();

struct gc_object;

gc_object *gc_object_alloc( int size );
void gc_object_free( gc_object *p );

struct gc_object{
	gc_object *succ;
	gc_object *pred;
	int flags;
	
	virtual ~gc_object(){
	}
	
	virtual void mark(){
	}
	
	void *operator new( size_t size ){
		return gc_object_alloc( size );
	}
	
	void operator delete( void *p ){
		gc_object_free( (gc_object*)p );
	}
};

gc_object gc_free_list;
gc_object gc_marked_list;
gc_object gc_unmarked_list;
gc_object gc_queued_list;	//doesn't really need to be doubly linked...

int gc_free_bytes;
int gc_marked_bytes;
int gc_alloced_bytes;
int gc_max_alloced_bytes;
int gc_new_bytes;
int gc_markbit=1;

gc_object *gc_cache[8];

void gc_collect_all();
void gc_mark_queued( int n );

#define GC_CLEAR_LIST( LIST ) ((LIST).succ=(LIST).pred=&(LIST))

#define GC_LIST_IS_EMPTY( LIST ) ((LIST).succ==&(LIST))

#define GC_REMOVE_NODE( NODE ){\
(NODE)->pred->succ=(NODE)->succ;\
(NODE)->succ->pred=(NODE)->pred;}

#define GC_INSERT_NODE( NODE,SUCC ){\
(NODE)->pred=(SUCC)->pred;\
(NODE)->succ=(SUCC);\
(SUCC)->pred->succ=(NODE);\
(SUCC)->pred=(NODE);}

void gc_init1(){
	GC_CLEAR_LIST( gc_free_list );
	GC_CLEAR_LIST( gc_marked_list );
	GC_CLEAR_LIST( gc_unmarked_list);
	GC_CLEAR_LIST( gc_queued_list );
}

void gc_init2(){
	gc_mark_roots();
}

#if CFG_CPP_GC_MODE==2

int gc_ctor_nest;
gc_object *gc_locals[CFG_CPP_GC_MAX_LOCALS],**gc_locals_sp=gc_locals;

struct gc_ctor{
	gc_ctor(){ ++gc_ctor_nest; }
	~gc_ctor(){ --gc_ctor_nest; }
};

struct gc_enter{
	gc_object **sp;
	gc_enter():sp(gc_locals_sp){
	}
	~gc_enter(){
#if DEBUG_GC
		static int max_locals;
		int n=gc_locals_sp-gc_locals;
		if( n>max_locals ){
			max_locals=n;
			printf( "max_locals=%i\n",n );
		}
#endif		
		gc_locals_sp=sp;
	}
};

#define GC_CTOR gc_ctor _c;
#define GC_ENTER gc_enter _e;

#else

struct gc_ctor{
};
struct gc_enter{
};

#define GC_CTOR
#define GC_ENTER

#endif

//Can be modified off thread!
static volatile int gc_ext_new_bytes;

#if _MSC_VER
#define atomic_add(P,V) InterlockedExchangeAdd((volatile unsigned int*)P,V)//(*(P)+=(V))
#define atomic_sub(P,V) InterlockedExchangeSubtract((volatile unsigned int*)P,V)//(*(P)-=(V))
#else
#define atomic_add(P,V) __sync_fetch_and_add(P,V)
#define atomic_sub(P,V) __sync_fetch_and_sub(P,V)
#endif

//Careful! May be called off thread!
//
void gc_ext_malloced( int size ){
	atomic_add( &gc_ext_new_bytes,size );
}

void gc_object_free( gc_object *p ){

	int size=p->flags & ~7;
	gc_free_bytes-=size;
	
	if( size<64 ){
		p->succ=gc_cache[size>>3];
		gc_cache[size>>3]=p;
	}else{
		free( p );
	}
}

void gc_flush_free( int size ){

	int t=gc_free_bytes-size;
	if( t<0 ) t=0;
	
	while( gc_free_bytes>t ){
		gc_object *p=gc_free_list.succ;

		GC_REMOVE_NODE( p );
	
#if DEBUG_GC
//		printf( "deleting @%p\n",p );fflush( stdout );
//		p->flags|=4;
//		continue;
		delete p;
#else
		delete p;
#endif
	}
}

gc_object *gc_object_alloc( int size ){

	size=(size+7)&~7;
	
	gc_new_bytes+=size;
	
#if CFG_CPP_GC_MODE==2

	if( !gc_ctor_nest ){

#if DEBUG_GC
		int ms=gc_micros();
#endif
		if( gc_new_bytes+gc_ext_new_bytes>(CFG_CPP_GC_TRIGGER) ){
			atomic_sub( &gc_ext_new_bytes,gc_ext_new_bytes );
			gc_collect_all();
			gc_new_bytes=0;
		}else{
			gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
		}

#if DEBUG_GC
		ms=gc_micros()-ms;
		if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif
	}
	
#endif

	gc_flush_free( size );

	gc_object *p;
	if( size<64 && (p=gc_cache[size>>3]) ){
		gc_cache[size>>3]=p->succ;
	}else{
		p=(gc_object*)malloc( size );
	}
	
	p->flags=size|gc_markbit;
	GC_INSERT_NODE( p,&gc_unmarked_list );

	gc_alloced_bytes+=size;
	if( gc_alloced_bytes>gc_max_alloced_bytes ) gc_max_alloced_bytes=gc_alloced_bytes;
	
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=p;
#endif

	return p;
}

#if DEBUG_GC

template<class T> gc_object *to_gc_object( T *t ){
	gc_object *p=dynamic_cast<gc_object*>(t);
	if( p && (p->flags & 4) ){
		printf( "gc error : object already deleted @%p\n",p );fflush( stdout );
		exit(-1);
	}
	return p;
}

#else

#define to_gc_object(t) dynamic_cast<gc_object*>(t)

#endif

template<class T> T *gc_retain( T *t ){
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=to_gc_object( t );
#endif
	return t;
}

template<class T> void gc_mark( T *t ){

	gc_object *p=to_gc_object( t );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

template<class T> void gc_mark_q( T *t ){

	gc_object *p=to_gc_object( t );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
}

template<class T,class V> void gc_assign( T *&lhs,V *rhs ){

	gc_object *p=to_gc_object( rhs );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
	lhs=rhs;
}

void gc_mark_locals(){
#if CFG_CPP_GC_MODE==2
	for( gc_object **pp=gc_locals;pp!=gc_locals_sp;++pp ){
		gc_object *p=*pp;
		if( p && (p->flags & 3)==gc_markbit ){
			p->flags^=1;
			GC_REMOVE_NODE( p );
			GC_INSERT_NODE( p,&gc_marked_list );
			gc_marked_bytes+=(p->flags & ~7);
			p->mark();
		}
	}
#endif	
}

void gc_mark_queued( int n ){
	while( gc_marked_bytes<n && !GC_LIST_IS_EMPTY( gc_queued_list ) ){
		gc_object *p=gc_queued_list.succ;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

//returns reclaimed bytes
void gc_sweep(){

	int reclaimed_bytes=gc_alloced_bytes-gc_marked_bytes;
	
	if( reclaimed_bytes ){
		//append unmarked list to end of free list
		gc_object *head=gc_unmarked_list.succ;
		gc_object *tail=gc_unmarked_list.pred;
		gc_object *succ=&gc_free_list;
		gc_object *pred=succ->pred;
		head->pred=pred;
		tail->succ=succ;
		pred->succ=head;
		succ->pred=tail;
		gc_free_bytes+=reclaimed_bytes;
	}
	
	//move marked to unmarked.
	gc_unmarked_list=gc_marked_list;
	gc_unmarked_list.succ->pred=gc_unmarked_list.pred->succ=&gc_unmarked_list;
	
	//clear marked.
	GC_CLEAR_LIST( gc_marked_list );
	
	//adjust sizes
	gc_alloced_bytes=gc_marked_bytes;
	gc_marked_bytes=0;
	gc_markbit^=1;
}

void gc_collect_all(){

//	printf( "Mark locals\n" );fflush( stdout );
	gc_mark_locals();

//	printf( "Mark queued\n" );fflush( stdout );
	gc_mark_queued( 0x7fffffff );

//	printf( "sweep\n" );fflush( stdout );	
	gc_sweep();

//	printf( "Mark roots\n" );fflush( stdout );
	gc_mark_roots();
}

void gc_collect(){
	
#if CFG_CPP_GC_MODE==1

#if DEBUG_GC
	int ms=gc_micros();
#endif

	if( gc_new_bytes+gc_ext_new_bytes>(CFG_CPP_GC_TRIGGER) ){
		atomic_sub( &gc_ext_new_bytes,gc_ext_new_bytes );
		gc_collect_all();
		gc_new_bytes=0;
	}else{
		gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
	}

#if DEBUG_GC
	ms=gc_micros()-ms;
	if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
//	if( ms>=0 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif

#endif
}

// ***** Array *****

template<class T> T *t_memcpy( T *dst,const T *src,int n ){
	memcpy( dst,src,n*sizeof(T) );
	return dst+n;
}

template<class T> T *t_memset( T *dst,int val,int n ){
	memset( dst,val,n*sizeof(T) );
	return dst+n;
}

template<class T> int t_memcmp( const T *x,const T *y,int n ){
	return memcmp( x,y,n*sizeof(T) );
}

template<class T> int t_strlen( const T *p ){
	const T *q=p++;
	while( *q++ ){}
	return q-p;
}

template<class T> T *t_create( int n,T *p ){
	t_memset( p,0,n );
	return p+n;
}

template<class T> T *t_create( int n,T *p,const T *q ){
	t_memcpy( p,q,n );
	return p+n;
}

template<class T> void t_destroy( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T **p ){
	for( int i=0;i<n;++i ) gc_mark( p[i] );
}

template<class T> class Array{
public:
	Array():rep( &nullRep ){
	}

	//Uses default...
//	Array( const Array<T> &t )...
	
	Array( int length ):rep( Rep::alloc( length ) ){
		t_create( rep->length,rep->data );
	}
	
	Array( const T *p,int length ):rep( Rep::alloc(length) ){
		t_create( rep->length,rep->data,p );
	}
	
	~Array(){
	}

	//Uses default...
//	Array &operator=( const Array &t )...
	
	int Length()const{ 
		return rep->length; 
	}
	
	T &At( int index ){
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	const T &At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	T &operator[]( int index ){
		return rep->data[index]; 
	}

	const T &operator[]( int index )const{
		return rep->data[index]; 
	}
	
	Array Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){ 
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<=from ) return Array();
		return Array( rep->data+from,term-from );
	}

	Array Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array Resize( int newlen )const{
		if( newlen<=0 ) return Array();
		int n=rep->length;
		if( newlen<n ) n=newlen;
		Rep *p=Rep::alloc( newlen );
		T *q=p->data;
		q=t_create( n,q,rep->data );
		q=t_create( (newlen-n),q );
		return Array( p );
	}
	
private:
	struct Rep : public gc_object{
		int length;
		T data[0];
		
		Rep():length(0){
			flags=3;
		}
		
		Rep( int length ):length(length){
		}
		
		~Rep(){
			t_destroy( length,data );
		}
		
		void mark(){
			gc_mark_elements( length,data );
		}
		
		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=gc_object_alloc( sizeof(Rep)+length*sizeof(T) );
			return ::new(p) Rep( length );
		}
		
	};
	Rep *rep;
	
	static Rep nullRep;
	
	template<class C> friend void gc_mark( Array<C> t );
	template<class C> friend void gc_mark_q( Array<C> t );
	template<class C> friend Array<C> gc_retain( Array<C> t );
	template<class C> friend void gc_assign( Array<C> &lhs,Array<C> rhs );
	template<class C> friend void gc_mark_elements( int n,Array<C> *p );
	
	Array( Rep *rep ):rep(rep){
	}
};

template<class T> typename Array<T>::Rep Array<T>::nullRep;

template<class T> Array<T> *t_create( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) *p++=Array<T>();
	return p;
}

template<class T> Array<T> *t_create( int n,Array<T> *p,const Array<T> *q ){
	for( int i=0;i<n;++i ) *p++=*q++;
	return p;
}

template<class T> void gc_mark( Array<T> t ){
	gc_mark( t.rep );
}

template<class T> void gc_mark_q( Array<T> t ){
	gc_mark_q( t.rep );
}

template<class T> Array<T> gc_retain( Array<T> t ){
#if CFG_CPP_GC_MODE==2
	gc_retain( t.rep );
#endif
	return t;
}

template<class T> void gc_assign( Array<T> &lhs,Array<T> rhs ){
	gc_mark( rhs.rep );
	lhs=rhs;
}

template<class T> void gc_mark_elements( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) gc_mark( p[i].rep );
}
		
// ***** String *****

static const char *_str_load_err;

class String{
public:
	String():rep( &nullRep ){
	}
	
	String( const String &t ):rep( t.rep ){
		rep->retain();
	}

	String( int n ){
		char buf[256];
		sprintf_s( buf,"%i",n );
		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}
	
	String( Float n ){
		char buf[256];
		
		//would rather use snprintf, but it's doing weird things in MingW.
		//
		sprintf_s( buf,"%.17lg",n );
		//
		char *p;
		for( p=buf;*p;++p ){
			if( *p=='.' || *p=='e' ) break;
		}
		if( !*p ){
			*p++='.';
			*p++='0';
			*p=0;
		}

		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}

	String( Char ch,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<length;++i ) rep->data[i]=ch;
	}

	String( const Char *p ):rep( Rep::alloc(t_strlen(p)) ){
		t_memcpy( rep->data,p,rep->length );
	}

	String( const Char *p,int length ):rep( Rep::alloc(length) ){
		t_memcpy( rep->data,p,rep->length );
	}
	
#if __OBJC__	
	String( NSString *nsstr ):rep( Rep::alloc([nsstr length]) ){
		unichar *buf=(unichar*)malloc( rep->length * sizeof(unichar) );
		[nsstr getCharacters:buf range:NSMakeRange(0,rep->length)];
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
		free( buf );
	}
#endif

#if __cplusplus_winrt
	String( Platform::String ^str ):rep( Rep::alloc(str->Length()) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=str->Data()[i];
	}
#endif

	~String(){
		rep->release();
	}
	
	template<class C> String( const C *p ):rep( Rep::alloc(t_strlen(p)) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	template<class C> String( const C *p,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	String Copy()const{
		Rep *crep=Rep::alloc( rep->length );
		t_memcpy( crep->data,rep->data,rep->length );
		return String( crep );
	}
	
	int Length()const{
		return rep->length;
	}
	
	const Char *Data()const{
		return rep->data;
	}
	
	Char At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Character index out of range" );
		return rep->data[index]; 
	}
	
	Char operator[]( int index )const{
		return rep->data[index];
	}
	
	String &operator=( const String &t ){
		t.rep->retain();
		rep->release();
		rep=t.rep;
		return *this;
	}
	
	String &operator+=( const String &t ){
		return operator=( *this+t );
	}
	
	int Compare( const String &t )const{
		int n=rep->length<t.rep->length ? rep->length : t.rep->length;
		for( int i=0;i<n;++i ){
			if( int q=(int)(rep->data[i])-(int)(t.rep->data[i]) ) return q;
		}
		return rep->length-t.rep->length;
	}
	
	bool operator==( const String &t )const{
		return rep->length==t.rep->length && t_memcmp( rep->data,t.rep->data,rep->length )==0;
	}
	
	bool operator!=( const String &t )const{
		return rep->length!=t.rep->length || t_memcmp( rep->data,t.rep->data,rep->length )!=0;
	}
	
	bool operator<( const String &t )const{
		return Compare( t )<0;
	}
	
	bool operator<=( const String &t )const{
		return Compare( t )<=0;
	}
	
	bool operator>( const String &t )const{
		return Compare( t )>0;
	}
	
	bool operator>=( const String &t )const{
		return Compare( t )>=0;
	}
	
	String operator+( const String &t )const{
		if( !rep->length ) return t;
		if( !t.rep->length ) return *this;
		Rep *p=Rep::alloc( rep->length+t.rep->length );
		Char *q=p->data;
		q=t_memcpy( q,rep->data,rep->length );
		q=t_memcpy( q,t.rep->data,t.rep->length );
		return String( p );
	}
	
	int Find( String find,int start=0 )const{
		if( start<0 ) start=0;
		while( start+find.rep->length<=rep->length ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			++start;
		}
		return -1;
	}
	
	int FindLast( String find )const{
		int start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	int FindLast( String find,int start )const{
		if( start>rep->length-find.rep->length ) start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	String Trim()const{
		int i=0,i2=rep->length;
		while( i<i2 && rep->data[i]<=32 ) ++i;
		while( i2>i && rep->data[i2-1]<=32 ) --i2;
		if( i==0 && i2==rep->length ) return *this;
		return String( rep->data+i,i2-i );
	}

	Array<String> Split( String sep )const{
	
		if( !sep.rep->length ){
			Array<String> bits( rep->length );
			for( int i=0;i<rep->length;++i ){
				bits[i]=String( (Char)(*this)[i],1 );
			}
			return bits;
		}
		
		int i=0,i2,n=1;
		while( (i2=Find( sep,i ))!=-1 ){
			++n;
			i=i2+sep.rep->length;
		}
		Array<String> bits( n );
		if( n==1 ){
			bits[0]=*this;
			return bits;
		}
		i=0;n=0;
		while( (i2=Find( sep,i ))!=-1 ){
			bits[n++]=Slice( i,i2 );
			i=i2+sep.rep->length;
		}
		bits[n]=Slice( i );
		return bits;
	}

	String Join( Array<String> bits )const{
		if( bits.Length()==0 ) return String();
		if( bits.Length()==1 ) return bits[0];
		int newlen=rep->length * (bits.Length()-1);
		for( int i=0;i<bits.Length();++i ){
			newlen+=bits[i].rep->length;
		}
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		q=t_memcpy( q,bits[0].rep->data,bits[0].rep->length );
		for( int i=1;i<bits.Length();++i ){
			q=t_memcpy( q,rep->data,rep->length );
			q=t_memcpy( q,bits[i].rep->data,bits[i].rep->length );
		}
		return String( p );
	}

	String Replace( String find,String repl )const{
		int i=0,i2,newlen=0;
		while( (i2=Find( find,i ))!=-1 ){
			newlen+=(i2-i)+repl.rep->length;
			i=i2+find.rep->length;
		}
		if( !i ) return *this;
		newlen+=rep->length-i;
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		i=0;
		while( (i2=Find( find,i ))!=-1 ){
			q=t_memcpy( q,rep->data+i,i2-i );
			q=t_memcpy( q,repl.rep->data,repl.rep->length );
			i=i2+find.rep->length;
		}
		q=t_memcpy( q,rep->data+i,rep->length-i );
		return String( p );
	}

	String ToLower()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towlower( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towlower( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}

	String ToUpper()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towupper( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towupper( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}
	
	bool Contains( String sub )const{
		return Find( sub )!=-1;
	}

	bool StartsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data,sub.rep->data,sub.rep->length );
	}

	bool EndsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data+rep->length-sub.rep->length,sub.rep->data,sub.rep->length );
	}
	
	String Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<from ) return String();
		if( from==0 && term==len ) return *this;
		return String( rep->data+from,term-from );
	}

	String Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array<int> ToChars()const{
		Array<int> chars( rep->length );
		for( int i=0;i<rep->length;++i ) chars[i]=rep->data[i];
		return chars;
	}
	
	int ToInt()const{
		char buf[64];
		return atoi( ToCString<char>( buf,sizeof(buf) ) );
	}
	
	Float ToFloat()const{
		char buf[256];
		return atof( ToCString<char>( buf,sizeof(buf) ) );
	}

	template<class C> class CString{
		struct Rep{
			int refs;
			C data[1];
		};
		Rep *_rep;
		static Rep _nul;
	public:
		template<class T> CString( const T *data,int length ){
			_rep=(Rep*)malloc( length*sizeof(C)+sizeof(Rep) );
			_rep->refs=1;
			_rep->data[length]=0;
			for( int i=0;i<length;++i ){
				_rep->data[i]=(C)data[i];
			}
		}
		CString():_rep( new Rep ){
			_rep->refs=1;
		}
		CString( const CString &c ):_rep(c._rep){
			++_rep->refs;
		}
		~CString(){
			if( !--_rep->refs ) free( _rep );
		}
		CString &operator=( const CString &c ){
			++c._rep->refs;
			if( !--_rep->refs ) free( _rep );
			_rep=c._rep;
			return *this;
		}
		operator const C*()const{ 
			return _rep->data;
		}
	};
	
	template<class C> CString<C> ToCString()const{
		return CString<C>( rep->data,rep->length );
	}

	template<class C> C *ToCString( C *p,int length )const{
		if( --length>rep->length ) length=rep->length;
		for( int i=0;i<length;++i ) p[i]=rep->data[i];
		p[length]=0;
		return p;
	}
	
#if __OBJC__	
	NSString *ToNSString()const{
		return [NSString stringWithCharacters:ToCString<unichar>() length:rep->length];
	}
#endif

#if __cplusplus_winrt
	Platform::String ^ToWinRTString()const{
		return ref new Platform::String( rep->data,rep->length );
	}
#endif

	bool Save( FILE *fp ){
		std::vector<unsigned char> buf;
		Save( buf );
		return buf.size() ? fwrite( &buf[0],1,buf.size(),fp )==buf.size() : true;
	}
	
	void Save( std::vector<unsigned char> &buf ){
	
		Char *p=rep->data;
		Char *e=p+rep->length;
		
		while( p<e ){
			Char c=*p++;
			if( c<0x80 ){
				buf.push_back( c );
			}else if( c<0x800 ){
				buf.push_back( 0xc0 | (c>>6) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}else{
				buf.push_back( 0xe0 | (c>>12) );
				buf.push_back( 0x80 | ((c>>6) & 0x3f) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}
		}
	}
	
	static String FromChars( Array<int> chars ){
		int n=chars.Length();
		Rep *p=Rep::alloc( n );
		for( int i=0;i<n;++i ){
			p->data[i]=chars[i];
		}
		return String( p );
	}

	static String Load( FILE *fp ){
		unsigned char tmp[4096];
		std::vector<unsigned char> buf;
		for(;;){
			int n=fread( tmp,1,4096,fp );
			if( n>0 ) buf.insert( buf.end(),tmp,tmp+n );
			if( n!=4096 ) break;
		}
		return buf.size() ? String::Load( &buf[0],buf.size() ) : String();
	}
	
	static String Load( unsigned char *p,int n ){
	
		_str_load_err=0;
		
		unsigned char *e=p+n;
		std::vector<Char> chars;
		
		int t0=n>0 ? p[0] : -1;
		int t1=n>1 ? p[1] : -1;

		if( t0==0xfe && t1==0xff ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (c<<8)|*p++ );
			}
		}else if( t0==0xff && t1==0xfe ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (*p++<<8)|c );
			}
		}else{
			int t2=n>2 ? p[2] : -1;
			if( t0==0xef && t1==0xbb && t2==0xbf ) p+=3;
			unsigned char *q=p;
			bool fail=false;
			while( p<e ){
				unsigned int c=*p++;
				if( c & 0x80 ){
					if( (c & 0xe0)==0xc0 ){
						if( p>=e || (p[0] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x1f)<<6) | (p[0] & 0x3f);
						p+=1;
					}else if( (c & 0xf0)==0xe0 ){
						if( p+1>=e || (p[0] & 0xc0)!=0x80 || (p[1] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x0f)<<12) | ((p[0] & 0x3f)<<6) | (p[1] & 0x3f);
						p+=2;
					}else{
						fail=true;
						break;
					}
				}
				chars.push_back( c );
			}
			if( fail ){
				_str_load_err="Invalid UTF-8";
				return String( q,n );
			}
		}
		return chars.size() ? String( &chars[0],chars.size() ) : String();
	}

private:
	
	struct Rep{
		int refs;
		int length;
		Char data[0];
		
		Rep():refs(1),length(0){
		}
		
		Rep( int length ):refs(1),length(length){
		}
		
		void retain(){
//			atomic_add( &refs,1 );
			++refs;
		}
		
		void release(){
//			if( atomic_sub( &refs,1 )>1 || this==&nullRep ) return;
			if( --refs || this==&nullRep ) return;
			free( this );
		}

		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=malloc( sizeof(Rep)+length*sizeof(Char) );
			return new(p) Rep( length );
		}
	};
	Rep *rep;
	
	static Rep nullRep;
	
	String( Rep *rep ):rep(rep){
	}
};

String::Rep String::nullRep;

String *t_create( int n,String *p ){
	for( int i=0;i<n;++i ) new( &p[i] ) String();
	return p+n;
}

String *t_create( int n,String *p,const String *q ){
	for( int i=0;i<n;++i ) new( &p[i] ) String( q[i] );
	return p+n;
}

void t_destroy( int n,String *p ){
	for( int i=0;i<n;++i ) p[i].~String();
}

// ***** Object *****

String dbg_stacktrace();

class Object : public gc_object{
public:
	virtual bool Equals( Object *obj ){
		return this==obj;
	}
	
	virtual int Compare( Object *obj ){
		return (char*)this-(char*)obj;
	}
	
	virtual String debug(){
		return "+Object\n";
	}
};

class ThrowableObject : public Object{
#ifndef NDEBUG
public:
	String stackTrace;
	ThrowableObject():stackTrace( dbg_stacktrace() ){}
#endif
};

struct gc_interface{
	virtual ~gc_interface(){}
};

//***** Debugger *****

//#define Error bbError
//#define Print bbPrint

int bbPrint( String t );

#define dbg_stream stderr

#if _MSC_VER
#define dbg_typeof decltype
#else
#define dbg_typeof __typeof__
#endif 

struct dbg_func;
struct dbg_var_type;

static int dbg_suspend;
static int dbg_stepmode;

const char *dbg_info;
String dbg_exstack;

static void *dbg_var_buf[65536*3];
static void **dbg_var_ptr=dbg_var_buf;

static dbg_func *dbg_func_buf[1024];
static dbg_func **dbg_func_ptr=dbg_func_buf;

String dbg_type( bool *p ){
	return "Bool";
}

String dbg_type( int *p ){
	return "Int";
}

String dbg_type( Float *p ){
	return "Float";
}

String dbg_type( String *p ){
	return "String";
}

template<class T> String dbg_type( T *p ){
	return "Object";
}

template<class T> String dbg_type( Array<T> *p ){
	return dbg_type( &(*p)[0] )+"[]";
}

String dbg_value( bool *p ){
	return *p ? "True" : "False";
}

String dbg_value( int *p ){
	return String( *p );
}

String dbg_value( Float *p ){
	return String( *p );
}

String dbg_value( String *p ){
	String t=*p;
	if( t.Length()>100 ) t=t.Slice( 0,100 )+"...";
	t=t.Replace( "\"","~q" );
	t=t.Replace( "\t","~t" );
	t=t.Replace( "\n","~n" );
	t=t.Replace( "\r","~r" );
	return String("\"")+t+"\"";
}

template<class T> String dbg_value( T *t ){
	Object *p=dynamic_cast<Object*>( *t );
	char buf[64];
	sprintf_s( buf,"%p",p );
	return String("@") + (buf[0]=='0' && buf[1]=='x' ? buf+2 : buf );
}

template<class T> String dbg_value( Array<T> *p ){
	String t="[";
	int n=(*p).Length();
	for( int i=0;i<n;++i ){
		if( i ) t+=",";
		t+=dbg_value( &(*p)[i] );
	}
	return t+"]";
}

template<class T> String dbg_decl( const char *id,T *ptr ){
	return String( id )+":"+dbg_type(ptr)+"="+dbg_value(ptr)+"\n";
}

struct dbg_var_type{
	virtual String type( void *p )=0;
	virtual String value( void *p )=0;
};

template<class T> struct dbg_var_type_t : public dbg_var_type{

	String type( void *p ){
		return dbg_type( (T*)p );
	}
	
	String value( void *p ){
		return dbg_value( (T*)p );
	}
	
	static dbg_var_type_t<T> info;
};
template<class T> dbg_var_type_t<T> dbg_var_type_t<T>::info;

struct dbg_blk{
	void **var_ptr;
	
	dbg_blk():var_ptr(dbg_var_ptr){
		if( dbg_stepmode=='l' ) --dbg_suspend;
	}
	
	~dbg_blk(){
		if( dbg_stepmode=='l' ) ++dbg_suspend;
		dbg_var_ptr=var_ptr;
	}
};

struct dbg_func : public dbg_blk{
	const char *id;
	const char *info;

	dbg_func( const char *p ):id(p),info(dbg_info){
		*dbg_func_ptr++=this;
		if( dbg_stepmode=='s' ) --dbg_suspend;
	}
	
	~dbg_func(){
		if( dbg_stepmode=='s' ) ++dbg_suspend;
		--dbg_func_ptr;
		dbg_info=info;
	}
};

int dbg_print( String t ){
	static char *buf;
	static int len;
	int n=t.Length();
	if( n+100>len ){
		len=n+100;
		free( buf );
		buf=(char*)malloc( len );
	}
	buf[n]='\n';
	for( int i=0;i<n;++i ) buf[i]=t[i];
	fwrite( buf,n+1,1,dbg_stream );
	fflush( dbg_stream );
	return 0;
}

void dbg_callstack(){

	void **var_ptr=dbg_var_buf;
	dbg_func **func_ptr=dbg_func_buf;
	
	while( var_ptr!=dbg_var_ptr ){
		while( func_ptr!=dbg_func_ptr && var_ptr==(*func_ptr)->var_ptr ){
			const char *id=(*func_ptr++)->id;
			const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
			fprintf( dbg_stream,"+%s;%s\n",id,info );
		}
		void *vp=*var_ptr++;
		const char *nm=(const char*)*var_ptr++;
		dbg_var_type *ty=(dbg_var_type*)*var_ptr++;
		dbg_print( String(nm)+":"+ty->type(vp)+"="+ty->value(vp) );
	}
	while( func_ptr!=dbg_func_ptr ){
		const char *id=(*func_ptr++)->id;
		const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
		fprintf( dbg_stream,"+%s;%s\n",id,info );
	}
}

String dbg_stacktrace(){
	if( !dbg_info || !dbg_info[0] ) return "";
	String str=String( dbg_info )+"\n";
	dbg_func **func_ptr=dbg_func_ptr;
	if( func_ptr==dbg_func_buf ) return str;
	while( --func_ptr!=dbg_func_buf ){
		str+=String( (*func_ptr)->info )+"\n";
	}
	return str;
}

void dbg_throw( const char *err ){
	dbg_exstack=dbg_stacktrace();
	throw err;
}

void dbg_stop(){

#if TARGET_OS_IPHONE
	dbg_throw( "STOP" );
#endif

	fprintf( dbg_stream,"{{~~%s~~}}\n",dbg_info );
	dbg_callstack();
	dbg_print( "" );
	
	for(;;){

		char buf[256];
		char *e=fgets( buf,256,stdin );
		if( !e ) exit( -1 );
		
		e=strchr( buf,'\n' );
		if( !e ) exit( -1 );
		
		*e=0;
		
		Object *p;
		
		switch( buf[0] ){
		case '?':
			break;
		case 'r':	//run
			dbg_suspend=0;		
			dbg_stepmode=0;
			return;
		case 's':	//step
			dbg_suspend=1;
			dbg_stepmode='s';
			return;
		case 'e':	//enter func
			dbg_suspend=1;
			dbg_stepmode='e';
			return;
		case 'l':	//leave block
			dbg_suspend=0;
			dbg_stepmode='l';
			return;
		case '@':	//dump object
			p=0;
			sscanf_s( buf+1,"%p",&p );
			if( p ){
				dbg_print( p->debug() );
			}else{
				dbg_print( "" );
			}
			break;
		case 'q':	//quit!
			exit( 0 );
			break;			
		default:
			printf( "????? %s ?????",buf );fflush( stdout );
			exit( -1 );
		}
	}
}

void dbg_error( const char *err ){

#if TARGET_OS_IPHONE
	dbg_throw( err );
#endif

	for(;;){
		bbPrint( String("Monkey Runtime Error : ")+err );
		bbPrint( dbg_stacktrace() );
		dbg_stop();
	}
}

#define DBG_INFO(X) dbg_info=(X);if( dbg_suspend>0 ) dbg_stop();

#define DBG_ENTER(P) dbg_func _dbg_func(P);

#define DBG_BLOCK() dbg_blk _dbg_blk;

#define DBG_GLOBAL( ID,NAME )	//TODO!

#define DBG_LOCAL( ID,NAME )\
*dbg_var_ptr++=&ID;\
*dbg_var_ptr++=(void*)NAME;\
*dbg_var_ptr++=&dbg_var_type_t<dbg_typeof(ID)>::info;

//**** main ****

int argc;
const char **argv;

Float D2R=0.017453292519943295f;
Float R2D=57.29577951308232f;

int bbPrint( String t ){

	static std::vector<unsigned char> buf;
	buf.clear();
	t.Save( buf );
	buf.push_back( '\n' );
	buf.push_back( 0 );
	
#if __cplusplus_winrt	//winrt?

#if CFG_WINRT_PRINT_ENABLED
	OutputDebugStringA( (const char*)&buf[0] );
#endif

#elif _WIN32			//windows?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );

#elif __APPLE__			//macos/ios?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
	
#elif __linux			//linux?

#if CFG_ANDROID_NDK_PRINT_ENABLED
	LOGI( (const char*)&buf[0] );
#else
	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
#endif

#endif

	return 0;
}

class BBExitApp{
};

int bbError( String err ){
	if( !err.Length() ){
#if __cplusplus_winrt
		throw BBExitApp();
#else
		exit( 0 );
#endif
	}
	dbg_error( err.ToCString<char>() );
	return 0;
}

int bbDebugLog( String t ){
	bbPrint( t );
	return 0;
}

int bbDebugStop(){
	dbg_stop();
	return 0;
}

int bbInit();
int bbMain();

#if _MSC_VER

static void _cdecl seTranslator( unsigned int ex,EXCEPTION_POINTERS *p ){

	switch( ex ){
	case EXCEPTION_ACCESS_VIOLATION:dbg_error( "Memory access violation" );
	case EXCEPTION_ILLEGAL_INSTRUCTION:dbg_error( "Illegal instruction" );
	case EXCEPTION_INT_DIVIDE_BY_ZERO:dbg_error( "Integer divide by zero" );
	case EXCEPTION_STACK_OVERFLOW:dbg_error( "Stack overflow" );
	}
	dbg_error( "Unknown exception" );
}

#else

void sighandler( int sig  ){
	switch( sig ){
	case SIGSEGV:dbg_error( "Memory access violation" );
	case SIGILL:dbg_error( "Illegal instruction" );
	case SIGFPE:dbg_error( "Floating point exception" );
#if !_WIN32
	case SIGBUS:dbg_error( "Bus error" );
#endif	
	}
	dbg_error( "Unknown signal" );
}

#endif

//entry point call by target main()...
//
int bb_std_main( int argc,const char **argv ){

	::argc=argc;
	::argv=argv;
	
#if _MSC_VER

	_set_se_translator( seTranslator );

#else
	
	signal( SIGSEGV,sighandler );
	signal( SIGILL,sighandler );
	signal( SIGFPE,sighandler );
#if !_WIN32
	signal( SIGBUS,sighandler );
#endif

#endif

	if( !setlocale( LC_CTYPE,"en_US.UTF-8" ) ){
		setlocale( LC_CTYPE,"" );
	}

	gc_init1();

	bbInit();
	
	gc_init2();

	bbMain();

	return 0;
}


//***** game.h *****

struct BBGameEvent{
	enum{
		None=0,
		KeyDown=1,KeyUp=2,KeyChar=3,
		MouseDown=4,MouseUp=5,MouseMove=6,
		TouchDown=7,TouchUp=8,TouchMove=9,
		MotionAccel=10
	};
};

class BBGameDelegate : public Object{
public:
	virtual void StartGame(){}
	virtual void SuspendGame(){}
	virtual void ResumeGame(){}
	virtual void UpdateGame(){}
	virtual void RenderGame(){}
	virtual void KeyEvent( int event,int data ){}
	virtual void MouseEvent( int event,int data,Float x,Float y ){}
	virtual void TouchEvent( int event,int data,Float x,Float y ){}
	virtual void MotionEvent( int event,int data,Float x,Float y,Float z ){}
	virtual void DiscardGraphics(){}
};

struct BBDisplayMode : public Object{
	int width;
	int height;
	int format;
	int hertz;
	int flags;
	BBDisplayMode( int width=0,int height=0,int format=0,int hertz=0,int flags=0 ):width(width),height(height),format(format),hertz(hertz),flags(flags){}
};

class BBGame{
public:
	BBGame();
	virtual ~BBGame(){}
	
	// ***** Extern *****
	static BBGame *Game(){ return _game; }
	
	virtual void SetDelegate( BBGameDelegate *delegate );
	virtual BBGameDelegate *Delegate(){ return _delegate; }
	
	virtual void SetKeyboardEnabled( bool enabled );
	virtual bool KeyboardEnabled();
	
	virtual void SetUpdateRate( int updateRate );
	virtual int UpdateRate();
	
	virtual bool Started(){ return _started; }
	virtual bool Suspended(){ return _suspended; }
	
	virtual int Millisecs();
	virtual void GetDate( Array<int> date );
	virtual int SaveState( String state );
	virtual String LoadState();
	virtual String LoadString( String path );
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
	
	virtual int GetDeviceWidth(){ return 0; }
	virtual int GetDeviceHeight(){ return 0; }
	virtual void SetDeviceWindow( int width,int height,int flags ){}
	virtual Array<BBDisplayMode*> GetDisplayModes(){ return Array<BBDisplayMode*>(); }
	virtual BBDisplayMode *GetDesktopMode(){ return 0; }
	virtual void SetSwapInterval( int interval ){}

	// ***** Native *****	
	virtual String PathToFilePath( String path );
	virtual FILE *OpenFile( String path,String mode );
	virtual unsigned char *LoadData( String path,int *plength );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *depth ){ return 0; }
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){ return 0; }
	
	//***** Internal *****
	virtual void Die( ThrowableObject *ex );
	virtual void gc_collect();
	virtual void StartGame();
	virtual void SuspendGame();
	virtual void ResumeGame();
	virtual void UpdateGame();
	virtual void RenderGame();
	virtual void KeyEvent( int ev,int data );
	virtual void MouseEvent( int ev,int data,float x,float y );
	virtual void TouchEvent( int ev,int data,float x,float y );
	virtual void MotionEvent( int ev,int data,float x,float y,float z );
	virtual void DiscardGraphics();
	
protected:

	static BBGame *_game;

	BBGameDelegate *_delegate;
	bool _keyboardEnabled;
	int _updateRate;
	bool _started;
	bool _suspended;
};

//***** game.cpp *****

BBGame *BBGame::_game;

BBGame::BBGame():
_delegate( 0 ),
_keyboardEnabled( false ),
_updateRate( 0 ),
_started( false ),
_suspended( false ){
	_game=this;
}

void BBGame::SetDelegate( BBGameDelegate *delegate ){
	_delegate=delegate;
}

void BBGame::SetKeyboardEnabled( bool enabled ){
	_keyboardEnabled=enabled;
}

bool BBGame::KeyboardEnabled(){
	return _keyboardEnabled;
}

void BBGame::SetUpdateRate( int updateRate ){
	_updateRate=updateRate;
}

int BBGame::UpdateRate(){
	return _updateRate;
}

int BBGame::Millisecs(){
	return 0;
}

void BBGame::GetDate( Array<int> date ){
	int n=date.Length();
	if( n>0 ){
		time_t t=time( 0 );
		
#if _MSC_VER
		struct tm tii;
		struct tm *ti=&tii;
		localtime_s( ti,&t );
#else
		struct tm *ti=localtime( &t );
#endif

		date[0]=ti->tm_year+1900;
		if( n>1 ){ 
			date[1]=ti->tm_mon+1;
			if( n>2 ){
				date[2]=ti->tm_mday;
				if( n>3 ){
					date[3]=ti->tm_hour;
					if( n>4 ){
						date[4]=ti->tm_min;
						if( n>5 ){
							date[5]=ti->tm_sec;
							if( n>6 ){
								date[6]=0;
							}
						}
					}
				}
			}
		}
	}
}

int BBGame::SaveState( String state ){
	if( FILE *f=OpenFile( "./.monkeystate","wb" ) ){
		bool ok=state.Save( f );
		fclose( f );
		return ok ? 0 : -2;
	}
	return -1;
}

String BBGame::LoadState(){
	if( FILE *f=OpenFile( "./.monkeystate","rb" ) ){
		String str=String::Load( f );
		fclose( f );
		return str;
	}
	return "";
}

String BBGame::LoadString( String path ){
	if( FILE *fp=OpenFile( path,"rb" ) ){
		String str=String::Load( fp );
		fclose( fp );
		return str;
	}
	return "";
}

bool BBGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){
	return false;
}

void BBGame::OpenUrl( String url ){
}

void BBGame::SetMouseVisible( bool visible ){
}

//***** C++ Game *****

String BBGame::PathToFilePath( String path ){
	return path;
}

FILE *BBGame::OpenFile( String path,String mode ){
	path=PathToFilePath( path );
	if( path=="" ) return 0;
	
#if __cplusplus_winrt
	path=path.Replace( "/","\\" );
	FILE *f;
	if( _wfopen_s( &f,path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() ) ) return 0;
	return f;
#elif _WIN32
	return _wfopen( path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() );
#else
	return fopen( path.ToCString<char>(),mode.ToCString<char>() );
#endif
}

unsigned char *BBGame::LoadData( String path,int *plength ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;

	const int BUF_SZ=4096;
	std::vector<void*> tmps;
	int length=0;
	
	for(;;){
		void *p=malloc( BUF_SZ );
		int n=fread( p,1,BUF_SZ,f );
		tmps.push_back( p );
		length+=n;
		if( n!=BUF_SZ ) break;
	}
	fclose( f );
	
	unsigned char *data=(unsigned char*)malloc( length );
	unsigned char *p=data;
	
	int sz=length;
	for( int i=0;i<tmps.size();++i ){
		int n=sz>BUF_SZ ? BUF_SZ : sz;
		memcpy( p,tmps[i],n );
		free( tmps[i] );
		sz-=n;
		p+=n;
	}
	
	*plength=length;
	
	gc_ext_malloced( length );
	
	return data;
}

//***** INTERNAL *****

void BBGame::Die( ThrowableObject *ex ){
	bbPrint( "Monkey Runtime Error : Uncaught Monkey Exception" );
#ifndef NDEBUG
	bbPrint( ex->stackTrace );
#endif
	exit( -1 );
}

void BBGame::gc_collect(){
	gc_mark( _delegate );
	::gc_collect();
}

void BBGame::StartGame(){

	if( _started ) return;
	_started=true;
	
	try{
		_delegate->StartGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::SuspendGame(){

	if( !_started || _suspended ) return;
	_suspended=true;
	
	try{
		_delegate->SuspendGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::ResumeGame(){

	if( !_started || !_suspended ) return;
	_suspended=false;
	
	try{
		_delegate->ResumeGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::UpdateGame(){

	if( !_started || _suspended ) return;
	
	try{
		_delegate->UpdateGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::RenderGame(){

	if( !_started ) return;
	
	try{
		_delegate->RenderGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::KeyEvent( int ev,int data ){

	if( !_started ) return;
	
	try{
		_delegate->KeyEvent( ev,data );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MouseEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->MouseEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::TouchEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->TouchEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MotionEvent( int ev,int data,float x,float y,float z ){

	if( !_started ) return;
	
	try{
		_delegate->MotionEvent( ev,data,x,y,z );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::DiscardGraphics(){

	if( !_started ) return;
	
	try{
		_delegate->DiscardGraphics();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}


//***** wavloader.h *****
//
unsigned char *LoadWAV( FILE *f,int *length,int *channels,int *format,int *hertz );

//***** wavloader.cpp *****
//
static const char *readTag( FILE *f ){
	static char buf[8];
	if( fread( buf,4,1,f )!=1 ) return "";
	buf[4]=0;
	return buf;
}

static int readInt( FILE *f ){
	unsigned char buf[4];
	if( fread( buf,4,1,f )!=1 ) return -1;
	return (buf[3]<<24) | (buf[2]<<16) | (buf[1]<<8) | buf[0];
}

static int readShort( FILE *f ){
	unsigned char buf[2];
	if( fread( buf,2,1,f )!=1 ) return -1;
	return (buf[1]<<8) | buf[0];
}

static void skipBytes( int n,FILE *f ){
	char *p=(char*)malloc( n );
	fread( p,n,1,f );
	free( p );
}

unsigned char *LoadWAV( FILE *f,int *plength,int *pchannels,int *pformat,int *phertz ){
	if( !strcmp( readTag( f ),"RIFF" ) ){
		int len=readInt( f )-8;len=len;
		if( !strcmp( readTag( f ),"WAVE" ) ){
			if( !strcmp( readTag( f ),"fmt " ) ){
				int len2=readInt( f );
				int comp=readShort( f );
				if( comp==1 ){
					int chans=readShort( f );
					int hertz=readInt( f );
					int bytespersec=readInt( f );bytespersec=bytespersec;
					int pad=readShort( f );pad=pad;
					int bits=readShort( f );
					int format=bits/8;
					if( len2>16 ) skipBytes( len2-16,f );
					for(;;){
						const char *p=readTag( f );
						if( feof( f ) ) break;
						int size=readInt( f );
						if( strcmp( p,"data" ) ){
							skipBytes( size,f );
							continue;
						}
						unsigned char *data=(unsigned char*)malloc( size );
						if( fread( data,size,1,f )==1 ){
							*plength=size/(chans*format);
							*pchannels=chans;
							*pformat=format;
							*phertz=hertz;
							return data;
						}
						free( data );
					}
				}
			}
		}
	}
	return 0;
}



//***** oggloader.h *****
unsigned char *LoadOGG( FILE *f,int *length,int *channels,int *format,int *hertz );

//***** oggloader.cpp *****
unsigned char *LoadOGG( FILE *f,int *length,int *channels,int *format,int *hertz ){

	int error;
	stb_vorbis *v=stb_vorbis_open_file( f,0,&error,0 );
	if( !v ) return 0;
	
	stb_vorbis_info info=stb_vorbis_get_info( v );
	
	int limit=info.channels*4096;
	int offset=0,total=limit;

	short *data=(short*)malloc( total*2 );
	
	for(;;){
		int n=stb_vorbis_get_frame_short_interleaved( v,info.channels,data+offset,total-offset );
		if( !n ) break;
	
		offset+=n*info.channels;
		
		if( offset+limit>total ){
			total*=2;
			data=(short*)realloc( data,total*2 );
		}
	}
	
	data=(short*)realloc( data,offset*2 );
	
	*length=offset/info.channels;
	*channels=info.channels;
	*format=2;
	*hertz=info.sample_rate;
	
	stb_vorbis_close(v);
	
	return (unsigned char*)data;
}



//***** glfwgame.h *****

struct BBGlfwVideoMode : public Object{
	int Width;
	int Height;
	int RedBits;
	int GreenBits;
	int BlueBits;
	BBGlfwVideoMode( int w,int h,int r,int g,int b ):Width(w),Height(h),RedBits(r),GreenBits(g),BlueBits(b){}
};

class BBGlfwGame : public BBGame{
public:
	BBGlfwGame();

	static BBGlfwGame *GlfwGame(){ return _glfwGame; }
	
	virtual void SetUpdateRate( int hertz );
	virtual int Millisecs();
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
	
	virtual int GetDeviceWidth();
	virtual int GetDeviceHeight();
	virtual void SetDeviceWindow( int width,int height,int flags );
	virtual Array<BBDisplayMode*> GetDisplayModes();
	virtual BBDisplayMode *GetDesktopMode();
	virtual void SetSwapInterval( int interval );

	virtual String PathToFilePath( String path );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *depth );
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz );
	
	virtual void SetGlfwWindow( int width,int height,int red,int green,int blue,int alpha,int depth,int stencil,bool fullscreen );
	virtual BBGlfwVideoMode *GetGlfwDesktopMode();
	virtual Array<BBGlfwVideoMode*> GetGlfwVideoModes();
	
	virtual void Run();
	
private:
	static BBGlfwGame *_glfwGame;

	double _updatePeriod;
	double _nextUpdate;
	
	int _swapInterval;
	
	void UpdateEvents();
		
protected:
	static int TransKey( int key );
	static int KeyToChar( int key );
	
	static void GLFWCALL OnKey( int key,int action );
	static void GLFWCALL OnChar( int chr,int action );
	static void GLFWCALL OnMouseButton( int button,int action );
	static void GLFWCALL OnMousePos( int x,int y );
	static int  GLFWCALL OnWindowClose();
};

//***** glfwgame.cpp *****

#define _QUOTE(X) #X
#define _STRINGIZE( X ) _QUOTE(X)

enum{
	VKEY_BACKSPACE=8,VKEY_TAB,
	VKEY_ENTER=13,
	VKEY_SHIFT=16,
	VKEY_CONTROL=17,
	VKEY_ESC=27,
	VKEY_SPACE=32,
	VKEY_PAGEUP=33,VKEY_PAGEDOWN,VKEY_END,VKEY_HOME,
	VKEY_LEFT=37,VKEY_UP,VKEY_RIGHT,VKEY_DOWN,
	VKEY_INSERT=45,VKEY_DELETE,
	VKEY_0=48,VKEY_1,VKEY_2,VKEY_3,VKEY_4,VKEY_5,VKEY_6,VKEY_7,VKEY_8,VKEY_9,
	VKEY_A=65,VKEY_B,VKEY_C,VKEY_D,VKEY_E,VKEY_F,VKEY_G,VKEY_H,VKEY_I,VKEY_J,
	VKEY_K,VKEY_L,VKEY_M,VKEY_N,VKEY_O,VKEY_P,VKEY_Q,VKEY_R,VKEY_S,VKEY_T,
	VKEY_U,VKEY_V,VKEY_W,VKEY_X,VKEY_Y,VKEY_Z,
	
	VKEY_LSYS=91,VKEY_RSYS,
	
	VKEY_NUM0=96,VKEY_NUM1,VKEY_NUM2,VKEY_NUM3,VKEY_NUM4,
	VKEY_NUM5,VKEY_NUM6,VKEY_NUM7,VKEY_NUM8,VKEY_NUM9,
	VKEY_NUMMULTIPLY=106,VKEY_NUMADD,VKEY_NUMSLASH,
	VKEY_NUMSUBTRACT,VKEY_NUMDECIMAL,VKEY_NUMDIVIDE,

	VKEY_F1=112,VKEY_F2,VKEY_F3,VKEY_F4,VKEY_F5,VKEY_F6,
	VKEY_F7,VKEY_F8,VKEY_F9,VKEY_F10,VKEY_F11,VKEY_F12,

	VKEY_LSHIFT=160,VKEY_RSHIFT,
	VKEY_LCONTROL=162,VKEY_RCONTROL,
	VKEY_LALT=164,VKEY_RALT,

	VKEY_TILDE=192,VKEY_MINUS=189,VKEY_EQUALS=187,
	VKEY_OPENBRACKET=219,VKEY_BACKSLASH=220,VKEY_CLOSEBRACKET=221,
	VKEY_SEMICOLON=186,VKEY_QUOTES=222,
	VKEY_COMMA=188,VKEY_PERIOD=190,VKEY_SLASH=191
};

BBGlfwGame *BBGlfwGame::_glfwGame;

BBGlfwGame::BBGlfwGame():_updatePeriod(0),_nextUpdate(0),_swapInterval( CFG_GLFW_SWAP_INTERVAL ){
	_glfwGame=this;
}

//***** BBGame *****

void Init_GL_Exts();

int glfwGraphicsSeq=0;

void BBGlfwGame::SetUpdateRate( int updateRate ){
	BBGame::SetUpdateRate( updateRate );
	if( _updateRate ) _updatePeriod=1.0/_updateRate;
	_nextUpdate=0;
}

int BBGlfwGame::Millisecs(){
	return int( glfwGetTime()*1000.0 );
}

bool BBGlfwGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){

	int joy=GLFW_JOYSTICK_1+port;
	if( !glfwGetJoystickParam( joy,GLFW_PRESENT ) ) return false;

	//read axes
	float axes[6];
	memset( axes,0,sizeof(axes) );
	int n_axes=glfwGetJoystickPos( joy,axes,6 );
	joyx[0]=axes[0];joyy[0]=axes[1];joyz[0]=axes[2];
	joyx[1]=axes[3];joyy[1]=axes[4];joyz[1]=axes[5];
	
	//read buttons
	unsigned char buts[32];
	memset( buts,0,sizeof(buts) );
	int n_buts=glfwGetJoystickButtons( joy,buts,32 );
	if( n_buts>12 ){
		for( int i=0;i<8;++i ) buttons[i]=(buts[i]==GLFW_PRESS);
		for( int i=0;i<4;++i ) buttons[i+8]=(buts[n_buts-4+i]==GLFW_PRESS);
		for( int i=0;i<n_buts-12;++i ) buttons[i+12]=(buts[i+8]==GLFW_PRESS);
	}else{
		for( int i=0;i<n_buts;++i ) buttons[i]=(buts[i]=-GLFW_PRESS);
	}
	
	//kludges for device type!
	if( n_axes==5 && n_buts==14 ){
		//XBOX_360?
		joyx[1]=axes[4];
		joyy[1]=-axes[3];
	}else if( n_axes==4 && n_buts==18 ){
		//My Saitek?
		joyy[1]=-joyz[0];
	}
	
	//enough!
	return true;
}

void BBGlfwGame::OpenUrl( String url ){
#if _WIN32
	ShellExecute( HWND_DESKTOP,"open",url.ToCString<char>(),0,0,SW_SHOWNORMAL );
#elif __APPLE__
	if( CFURLRef cfurl=CFURLCreateWithBytes( 0,url.ToCString<UInt8>(),url.Length(),kCFStringEncodingASCII,0 ) ){
		LSOpenCFURLRef( cfurl,0 );
		CFRelease( cfurl );
	}
#elif __linux
	system( ( String( "xdg-open \"" )+url+"\"" ).ToCString<char>() );
#endif
}

void BBGlfwGame::SetMouseVisible( bool visible ){
	if( visible ){
		glfwEnable( GLFW_MOUSE_CURSOR );
	}else{
		glfwDisable( GLFW_MOUSE_CURSOR );
	}
}

String BBGlfwGame::PathToFilePath( String path ){
	if( !path.StartsWith( "monkey:" ) ){
		return path;
	}else if( path.StartsWith( "monkey://data/" ) ){
		return String("./data/")+path.Slice(14);
	}else if( path.StartsWith( "monkey://internal/" ) ){
		return String("./internal/")+path.Slice(18);
	}else if( path.StartsWith( "monkey://external/" ) ){
		return String("./external/")+path.Slice(18);
	}
	return "";
}

unsigned char *BBGlfwGame::LoadImageData( String path,int *width,int *height,int *depth ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;
	
	unsigned char *data=stbi_load_from_file( f,width,height,depth,0 );
	fclose( f );
	
	if( data ) gc_ext_malloced( (*width)*(*height)*(*depth) );
	
	return data;
}

unsigned char *BBGlfwGame::LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;
	
	unsigned char *data=0;
	
	if( path.ToLower().EndsWith( ".wav" ) ){
		data=LoadWAV( f,length,channels,format,hertz );
	}else if( path.ToLower().EndsWith( ".ogg" ) ){
		data=LoadOGG( f,length,channels,format,hertz );
	}
	fclose( f );
	
	if( data ) gc_ext_malloced( (*length)*(*channels)*(*format) );
	
	return data;
}

//glfw key to monkey key!
int BBGlfwGame::TransKey( int key ){

	if( key>='0' && key<='9' ) return key;
	if( key>='A' && key<='Z' ) return key;

	switch( key ){

	case ' ':return VKEY_SPACE;
	case ';':return VKEY_SEMICOLON;
	case '=':return VKEY_EQUALS;
	case ',':return VKEY_COMMA;
	case '-':return VKEY_MINUS;
	case '.':return VKEY_PERIOD;
	case '/':return VKEY_SLASH;
	case '~':return VKEY_TILDE;
	case '[':return VKEY_OPENBRACKET;
	case ']':return VKEY_CLOSEBRACKET;
	case '\"':return VKEY_QUOTES;
	case '\\':return VKEY_BACKSLASH;
	
	case '`':return VKEY_TILDE;
	case '\'':return VKEY_QUOTES;

	case GLFW_KEY_LSHIFT:
	case GLFW_KEY_RSHIFT:return VKEY_SHIFT;
	case GLFW_KEY_LCTRL:
	case GLFW_KEY_RCTRL:return VKEY_CONTROL;
	
//	case GLFW_KEY_LSHIFT:return VKEY_LSHIFT;
//	case GLFW_KEY_RSHIFT:return VKEY_RSHIFT;
//	case GLFW_KEY_LCTRL:return VKEY_LCONTROL;
//	case GLFW_KEY_RCTRL:return VKEY_RCONTROL;
	
	case GLFW_KEY_BACKSPACE:return VKEY_BACKSPACE;
	case GLFW_KEY_TAB:return VKEY_TAB;
	case GLFW_KEY_ENTER:return VKEY_ENTER;
	case GLFW_KEY_ESC:return VKEY_ESC;
	case GLFW_KEY_INSERT:return VKEY_INSERT;
	case GLFW_KEY_DEL:return VKEY_DELETE;
	case GLFW_KEY_PAGEUP:return VKEY_PAGEUP;
	case GLFW_KEY_PAGEDOWN:return VKEY_PAGEDOWN;
	case GLFW_KEY_HOME:return VKEY_HOME;
	case GLFW_KEY_END:return VKEY_END;
	case GLFW_KEY_UP:return VKEY_UP;
	case GLFW_KEY_DOWN:return VKEY_DOWN;
	case GLFW_KEY_LEFT:return VKEY_LEFT;
	case GLFW_KEY_RIGHT:return VKEY_RIGHT;
	
	case GLFW_KEY_KP_0:return VKEY_NUM0;
	case GLFW_KEY_KP_1:return VKEY_NUM1;
	case GLFW_KEY_KP_2:return VKEY_NUM2;
	case GLFW_KEY_KP_3:return VKEY_NUM3;
	case GLFW_KEY_KP_4:return VKEY_NUM4;
	case GLFW_KEY_KP_5:return VKEY_NUM5;
	case GLFW_KEY_KP_6:return VKEY_NUM6;
	case GLFW_KEY_KP_7:return VKEY_NUM7;
	case GLFW_KEY_KP_8:return VKEY_NUM8;
	case GLFW_KEY_KP_9:return VKEY_NUM9;
	case GLFW_KEY_KP_DIVIDE:return VKEY_NUMDIVIDE;
	case GLFW_KEY_KP_MULTIPLY:return VKEY_NUMMULTIPLY;
	case GLFW_KEY_KP_SUBTRACT:return VKEY_NUMSUBTRACT;
	case GLFW_KEY_KP_ADD:return VKEY_NUMADD;
	case GLFW_KEY_KP_DECIMAL:return VKEY_NUMDECIMAL;
    	
	case GLFW_KEY_F1:return VKEY_F1;
	case GLFW_KEY_F2:return VKEY_F2;
	case GLFW_KEY_F3:return VKEY_F3;
	case GLFW_KEY_F4:return VKEY_F4;
	case GLFW_KEY_F5:return VKEY_F5;
	case GLFW_KEY_F6:return VKEY_F6;
	case GLFW_KEY_F7:return VKEY_F7;
	case GLFW_KEY_F8:return VKEY_F8;
	case GLFW_KEY_F9:return VKEY_F9;
	case GLFW_KEY_F10:return VKEY_F10;
	case GLFW_KEY_F11:return VKEY_F11;
	case GLFW_KEY_F12:return VKEY_F12;
	}
	return 0;
}

//monkey key to special monkey char
int BBGlfwGame::KeyToChar( int key ){
	switch( key ){
	case VKEY_BACKSPACE:
	case VKEY_TAB:
	case VKEY_ENTER:
	case VKEY_ESC:
		return key;
	case VKEY_PAGEUP:
	case VKEY_PAGEDOWN:
	case VKEY_END:
	case VKEY_HOME:
	case VKEY_LEFT:
	case VKEY_UP:
	case VKEY_RIGHT:
	case VKEY_DOWN:
	case VKEY_INSERT:
		return key | 0x10000;
	case VKEY_DELETE:
		return 127;
	}
	return 0;
}

void BBGlfwGame::OnMouseButton( int button,int action ){
	switch( button ){
	case GLFW_MOUSE_BUTTON_LEFT:button=0;break;
	case GLFW_MOUSE_BUTTON_RIGHT:button=1;break;
	case GLFW_MOUSE_BUTTON_MIDDLE:button=2;break;
	default:return;
	}
	int x,y;
	glfwGetMousePos( &x,&y );
	switch( action ){
	case GLFW_PRESS:
		_glfwGame->MouseEvent( BBGameEvent::MouseDown,button,x,y );
		break;
	case GLFW_RELEASE:
		_glfwGame->MouseEvent( BBGameEvent::MouseUp,button,x,y );
		break;
	}
}

void BBGlfwGame::OnMousePos( int x,int y ){
	_game->MouseEvent( BBGameEvent::MouseMove,-1,x,y );
}

int BBGlfwGame::OnWindowClose(){
	_game->KeyEvent( BBGameEvent::KeyDown,0x1b0 );
	_game->KeyEvent( BBGameEvent::KeyUp,0x1b0 );
	return GL_FALSE;
}

void BBGlfwGame::OnKey( int key,int action ){

	key=TransKey( key );
	if( !key ) return;
	
	switch( action ){
	case GLFW_PRESS:
		_glfwGame->KeyEvent( BBGameEvent::KeyDown,key );
		if( int chr=KeyToChar( key ) ) _game->KeyEvent( BBGameEvent::KeyChar,chr );
		break;
	case GLFW_RELEASE:
		_glfwGame->KeyEvent( BBGameEvent::KeyUp,key );
		break;
	}
}

void BBGlfwGame::OnChar( int chr,int action ){

	switch( action ){
	case GLFW_PRESS:
		_glfwGame->KeyEvent( BBGameEvent::KeyChar,chr );
		break;
	}
}

void BBGlfwGame::SetGlfwWindow( int width,int height,int red,int green,int blue,int alpha,int depth,int stencil,bool fullscreen ){

	for( int i=0;i<=GLFW_KEY_LAST;++i ){
		int key=TransKey( i );
		if( key && glfwGetKey( i )==GLFW_PRESS ) KeyEvent( BBGameEvent::KeyUp,key );
	}

	GLFWvidmode desktopMode;
	glfwGetDesktopMode( &desktopMode );

	glfwCloseWindow();
	
	glfwOpenWindowHint( GLFW_REFRESH_RATE,60 );
	glfwOpenWindowHint( GLFW_WINDOW_NO_RESIZE,CFG_GLFW_WINDOW_RESIZABLE ? GL_FALSE : GL_TRUE );

	glfwOpenWindow( width,height,red,green,blue,alpha,depth,stencil,fullscreen ? GLFW_FULLSCREEN : GLFW_WINDOW );

	++glfwGraphicsSeq;

	if( !fullscreen ){	
		glfwSetWindowPos( (desktopMode.Width-width)/2,(desktopMode.Height-height)/2 );
		glfwSetWindowTitle( _STRINGIZE(CFG_GLFW_WINDOW_TITLE) );
	}

#if CFG_OPENGL_INIT_EXTENSIONS
	Init_GL_Exts();
#endif

	if( _swapInterval>=0 ) glfwSwapInterval( _swapInterval );

	glfwEnable( GLFW_KEY_REPEAT );
	glfwDisable( GLFW_AUTO_POLL_EVENTS );
	glfwSetKeyCallback( OnKey );
	glfwSetCharCallback( OnChar );
	glfwSetMouseButtonCallback( OnMouseButton );
	glfwSetMousePosCallback( OnMousePos );
	glfwSetWindowCloseCallback(	OnWindowClose );
}

Array<BBGlfwVideoMode*> BBGlfwGame::GetGlfwVideoModes(){
	GLFWvidmode modes[1024];
	int n=glfwGetVideoModes( modes,1024 );
	Array<BBGlfwVideoMode*> bbmodes( n );
	for( int i=0;i<n;++i ){
		bbmodes[i]=new BBGlfwVideoMode( modes[i].Width,modes[i].Height,modes[i].RedBits,modes[i].GreenBits,modes[i].BlueBits );
	}
	return bbmodes;
}

BBGlfwVideoMode *BBGlfwGame::GetGlfwDesktopMode(){
	GLFWvidmode mode;
	glfwGetDesktopMode( &mode );
	return new BBGlfwVideoMode( mode.Width,mode.Height,mode.RedBits,mode.GreenBits,mode.BlueBits );
}

int BBGlfwGame::GetDeviceWidth(){
	int width,height;
	glfwGetWindowSize( &width,&height );
	return width;
}

int BBGlfwGame::GetDeviceHeight(){
	int width,height;
	glfwGetWindowSize( &width,&height );
	return height;
}

void BBGlfwGame::SetDeviceWindow( int width,int height,int flags ){

	SetGlfwWindow( width,height,8,8,8,0,CFG_OPENGL_DEPTH_BUFFER_ENABLED ? 32 : 0,0,(flags&1)!=0 );
}

Array<BBDisplayMode*> BBGlfwGame::GetDisplayModes(){

	GLFWvidmode vmodes[1024];
	int n=glfwGetVideoModes( vmodes,1024 );
	Array<BBDisplayMode*> modes( n );
	for( int i=0;i<n;++i ) modes[i]=new BBDisplayMode( vmodes[i].Width,vmodes[i].Height );
	return modes;
}

BBDisplayMode *BBGlfwGame::GetDesktopMode(){

	GLFWvidmode vmode;
	glfwGetDesktopMode( &vmode );
	return new BBDisplayMode( vmode.Width,vmode.Height );
}

void BBGlfwGame::SetSwapInterval( int interval ){
	_swapInterval=interval;
	if( _swapInterval>=0 ) glfwSwapInterval( _swapInterval );
}

void BBGlfwGame::UpdateEvents(){
	if( _suspended ){
		glfwWaitEvents();
	}else{
		glfwPollEvents();
	}
	if( glfwGetWindowParam( GLFW_ACTIVE ) ){
		if( _suspended ){
			ResumeGame();
			_nextUpdate=0;
		}
	}else if( glfwGetWindowParam( GLFW_ICONIFIED ) || CFG_MOJO_AUTO_SUSPEND_ENABLED ){
		if( !_suspended ){
			SuspendGame();
			_nextUpdate=0;
		}
	}
}

void BBGlfwGame::Run(){

#if	CFG_GLFW_WINDOW_WIDTH && CFG_GLFW_WINDOW_HEIGHT

	SetGlfwWindow( CFG_GLFW_WINDOW_WIDTH,CFG_GLFW_WINDOW_HEIGHT,8,8,8,0,CFG_OPENGL_DEPTH_BUFFER_ENABLED ? 32 : 0,0,CFG_GLFW_WINDOW_FULLSCREEN );

#endif

	StartGame();
	
	while( glfwGetWindowParam( GLFW_OPENED ) ){
	
		RenderGame();
		glfwSwapBuffers();
		
		if( _nextUpdate ){
			double delay=_nextUpdate-glfwGetTime();
			if( delay>0 ) glfwSleep( delay );
		}
		
		//Update user events
		UpdateEvents();

		//App suspended?		
		if( _suspended ) continue;

		//'Go nuts' mode!
		if( !_updateRate ){
			UpdateGame();
			continue;
		}
		
		//Reset update timer?
		if( !_nextUpdate ) _nextUpdate=glfwGetTime();
		
		//Catch up updates...
		int i=0;
		for( ;i<4;++i ){
		
			UpdateGame();
			if( !_nextUpdate ) break;
			
			_nextUpdate+=_updatePeriod;
			
			if( _nextUpdate>glfwGetTime() ) break;
		}
		
		if( i==4 ) _nextUpdate=0;
	}
}



//***** monkeygame.h *****

class BBMonkeyGame : public BBGlfwGame{
public:

	static void Main( int args,const char *argv[] );
};

//***** monkeygame.cpp *****

#define _QUOTE(X) #X
#define _STRINGIZE(X) _QUOTE(X)

void BBMonkeyGame::Main( int argc,const char *argv[] ){

	if( !glfwInit() ){
		puts( "glfwInit failed" );
		exit(-1);
	}

	BBMonkeyGame *game=new BBMonkeyGame();
	
	try{
	
		bb_std_main( argc,argv );
		
	}catch( ThrowableObject *ex ){
	
		glfwTerminate();
		
		game->Die( ex );
		
		return;
	}

	if( game->Delegate() ) game->Run();
	
	glfwTerminate();
}


// GLFW mojo runtime.
//
// Copyright 2011 Mark Sibly, all rights reserved.
// No warranty implied; use at your own risk.

//***** gxtkGraphics.h *****

class gxtkSurface;

class gxtkGraphics : public Object{
public:

	enum{
		MAX_VERTS=1024,
		MAX_QUADS=(MAX_VERTS/4)
	};

	int width;
	int height;

	int colorARGB;
	float r,g,b,alpha;
	float ix,iy,jx,jy,tx,ty;
	bool tformed;

	float vertices[MAX_VERTS*5];
	unsigned short quadIndices[MAX_QUADS*6];

	int primType;
	int vertCount;
	gxtkSurface *primSurf;
	
	gxtkGraphics();
	
	void Flush();
	float *Begin( int type,int count,gxtkSurface *surf );
	
	//***** GXTK API *****
	virtual int Width();
	virtual int Height();
	
	virtual int  BeginRender();
	virtual void EndRender();
	virtual void DiscardGraphics();

	virtual gxtkSurface *LoadSurface( String path );
	virtual gxtkSurface *CreateSurface( int width,int height );
	virtual bool LoadSurface__UNSAFE__( gxtkSurface *surface,String path );
	
	virtual int Cls( float r,float g,float b );
	virtual int SetAlpha( float alpha );
	virtual int SetColor( float r,float g,float b );
	virtual int SetBlend( int blend );
	virtual int SetScissor( int x,int y,int w,int h );
	virtual int SetMatrix( float ix,float iy,float jx,float jy,float tx,float ty );
	
	virtual int DrawPoint( float x,float y );
	virtual int DrawRect( float x,float y,float w,float h );
	virtual int DrawLine( float x1,float y1,float x2,float y2 );
	virtual int DrawOval( float x1,float y1,float x2,float y2 );
	virtual int DrawPoly( Array<Float> verts );
	virtual int DrawPoly2( Array<Float> verts,gxtkSurface *surface,int srcx,int srcy );
	virtual int DrawSurface( gxtkSurface *surface,float x,float y );
	virtual int DrawSurface2( gxtkSurface *surface,float x,float y,int srcx,int srcy,int srcw,int srch );
	
	virtual int ReadPixels( Array<int> pixels,int x,int y,int width,int height,int offset,int pitch );
	virtual int WritePixels2( gxtkSurface *surface,Array<int> pixels,int x,int y,int width,int height,int offset,int pitch );
};

class gxtkSurface : public Object{
public:
	unsigned char *data;
	int width;
	int height;
	int depth;
	int format;
	int seq;
	
	GLuint texture;
	float uscale;
	float vscale;
	
	gxtkSurface();
	
	void SetData( unsigned char *data,int width,int height,int depth );
	void SetSubData( int x,int y,int w,int h,unsigned *src,int pitch );
	void Bind();
	
	~gxtkSurface();
	
	//***** GXTK API *****
	virtual int Discard();
	virtual int Width();
	virtual int Height();
	virtual int Loaded();
	virtual void OnUnsafeLoadComplete();
};

//***** gxtkGraphics.cpp *****

#ifndef GL_BGRA
#define GL_BGRA  0x80e1
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812f
#endif

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

static int Pow2Size( int n ){
	int i=1;
	while( i<n ) i+=i;
	return i;
}

gxtkGraphics::gxtkGraphics(){

	width=height=0;
	vertCount=0;
	
#ifdef _glfw3_h_
	GLFWwindow *window=BBGlfwGame::GlfwGame()->GetGLFWwindow();
	if( window ) glfwGetWindowSize( BBGlfwGame::GlfwGame()->GetGLFWwindow(),&width,&height );
#else
	glfwGetWindowSize( &width,&height );
#endif
	
	if( CFG_OPENGL_GLES20_ENABLED ) return;
	
	for( int i=0;i<MAX_QUADS;++i ){
		quadIndices[i*6  ]=(short)(i*4);
		quadIndices[i*6+1]=(short)(i*4+1);
		quadIndices[i*6+2]=(short)(i*4+2);
		quadIndices[i*6+3]=(short)(i*4);
		quadIndices[i*6+4]=(short)(i*4+2);
		quadIndices[i*6+5]=(short)(i*4+3);
	}
}

void gxtkGraphics::Flush(){
	if( !vertCount ) return;

	if( primSurf ){
		glEnable( GL_TEXTURE_2D );
		primSurf->Bind();
	}
		
	switch( primType ){
	case 1:
		glDrawArrays( GL_POINTS,0,vertCount );
		break;
	case 2:
		glDrawArrays( GL_LINES,0,vertCount );
		break;
	case 3:
		glDrawArrays( GL_TRIANGLES,0,vertCount );
		break;
	case 4:
		glDrawElements( GL_TRIANGLES,vertCount/4*6,GL_UNSIGNED_SHORT,quadIndices );
		break;
	default:
		for( int j=0;j<vertCount;j+=primType ){
			glDrawArrays( GL_TRIANGLE_FAN,j,primType );
		}
		break;
	}

	if( primSurf ){
		glDisable( GL_TEXTURE_2D );
	}

	vertCount=0;
}

float *gxtkGraphics::Begin( int type,int count,gxtkSurface *surf ){
	if( primType!=type || primSurf!=surf || vertCount+count>MAX_VERTS ){
		Flush();
		primType=type;
		primSurf=surf;
	}
	float *vp=vertices+vertCount*5;
	vertCount+=count;
	return vp;
}

//***** GXTK API *****

int gxtkGraphics::Width(){
	return width;
}

int gxtkGraphics::Height(){
	return height;
}

int gxtkGraphics::BeginRender(){

	width=height=0;
#ifdef _glfw3_h_
	glfwGetWindowSize( BBGlfwGame::GlfwGame()->GetGLFWwindow(),&width,&height );
#else
	glfwGetWindowSize( &width,&height );
#endif
	
	if( CFG_OPENGL_GLES20_ENABLED ) return 0;
	
	glViewport( 0,0,width,height );

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0,width,height,0,-1,1 );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	
	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 2,GL_FLOAT,20,&vertices[0] );	
	
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2,GL_FLOAT,20,&vertices[2] );
	
	glEnableClientState( GL_COLOR_ARRAY );
	glColorPointer( 4,GL_UNSIGNED_BYTE,20,&vertices[4] );
	
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE,GL_ONE_MINUS_SRC_ALPHA );
	
	glDisable( GL_TEXTURE_2D );
	
	vertCount=0;
	
	return 1;
}

void gxtkGraphics::EndRender(){
	if( !CFG_OPENGL_GLES20_ENABLED ) Flush();
}

void gxtkGraphics::DiscardGraphics(){
}

int gxtkGraphics::Cls( float r,float g,float b ){
	vertCount=0;

	glClearColor( r/255.0f,g/255.0f,b/255.0f,1 );
	glClear( GL_COLOR_BUFFER_BIT );

	return 0;
}

int gxtkGraphics::SetAlpha( float alpha ){
	this->alpha=alpha;
	
	int a=int(alpha*255);
	
	colorARGB=(a<<24) | (int(b*alpha)<<16) | (int(g*alpha)<<8) | int(r*alpha);
	
	return 0;
}

int gxtkGraphics::SetColor( float r,float g,float b ){
	this->r=r;
	this->g=g;
	this->b=b;

	int a=int(alpha*255);
	
	colorARGB=(a<<24) | (int(b*alpha)<<16) | (int(g*alpha)<<8) | int(r*alpha);
	
	return 0;
}

int gxtkGraphics::SetBlend( int blend ){

	Flush();
	
	switch( blend ){
	case 1:
		glBlendFunc( GL_ONE,GL_ONE );
		break;
	default:
		glBlendFunc( GL_ONE,GL_ONE_MINUS_SRC_ALPHA );
	}

	return 0;
}

int gxtkGraphics::SetScissor( int x,int y,int w,int h ){

	Flush();
	
	if( x!=0 || y!=0 || w!=Width() || h!=Height() ){
		glEnable( GL_SCISSOR_TEST );
		y=Height()-y-h;
		glScissor( x,y,w,h );
	}else{
		glDisable( GL_SCISSOR_TEST );
	}
	return 0;
}

int gxtkGraphics::SetMatrix( float ix,float iy,float jx,float jy,float tx,float ty ){

	tformed=(ix!=1 || iy!=0 || jx!=0 || jy!=1 || tx!=0 || ty!=0);

	this->ix=ix;this->iy=iy;this->jx=jx;this->jy=jy;this->tx=tx;this->ty=ty;

	return 0;
}

int gxtkGraphics::DrawPoint( float x,float y ){

	if( tformed ){
		float px=x;
		x=px * ix + y * jx + tx;
		y=px * iy + y * jy + ty;
	}
	
	float *vp=Begin( 1,1,0 );
	
	vp[0]=x+.5f;vp[1]=y+.5f;(int&)vp[4]=colorARGB;

	return 0;	
}
	
int gxtkGraphics::DrawLine( float x0,float y0,float x1,float y1 ){

	if( tformed ){
		float tx0=x0,tx1=x1;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
	}
	
	float *vp=Begin( 2,2,0 );

	vp[0]=x0+.5f;vp[1]=y0+.5f;(int&)vp[4]=colorARGB;
	vp[5]=x1+.5f;vp[6]=y1+.5f;(int&)vp[9]=colorARGB;
	
	return 0;
}

int gxtkGraphics::DrawRect( float x,float y,float w,float h ){

	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,0 );

	vp[0 ]=x0;vp[1 ]=y0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;(int&)vp[19]=colorARGB;

	return 0;
}

int gxtkGraphics::DrawOval( float x,float y,float w,float h ){
	
	float xr=w/2.0f;
	float yr=h/2.0f;

	int n;
	if( tformed ){
		float dx_x=xr * ix;
		float dx_y=xr * iy;
		float dx=sqrtf( dx_x*dx_x+dx_y*dx_y );
		float dy_x=yr * jx;
		float dy_y=yr * jy;
		float dy=sqrtf( dy_x*dy_x+dy_y*dy_y );
		n=(int)( dx+dy );
	}else{
		n=(int)( abs( xr )+abs( yr ) );
	}
	
	if( n<12 ){
		n=12;
	}else if( n>MAX_VERTS ){
		n=MAX_VERTS;
	}else{
		n&=~3;
	}

	float x0=x+xr,y0=y+yr;
	
	float *vp=Begin( n,n,0 );

	for( int i=0;i<n;++i ){
	
		float th=i * 6.28318531f / n;

		float px=x0+cosf( th ) * xr;
		float py=y0-sinf( th ) * yr;
		
		if( tformed ){
			float ppx=px;
			px=ppx * ix + py * jx + tx;
			py=ppx * iy + py * jy + ty;
		}
		
		vp[0]=px;vp[1]=py;(int&)vp[4]=colorARGB;
		vp+=5;
	}
	
	return 0;
}

int gxtkGraphics::DrawPoly( Array<Float> verts ){

	int n=verts.Length()/2;
	if( n<1 || n>MAX_VERTS ) return 0;
	
	float *vp=Begin( n,n,0 );
	
	for( int i=0;i<n;++i ){
		int j=i*2;
		if( tformed ){
			vp[0]=verts[j] * ix + verts[j+1] * jx + tx;
			vp[1]=verts[j] * iy + verts[j+1] * jy + ty;
		}else{
			vp[0]=verts[j];
			vp[1]=verts[j+1];
		}
		(int&)vp[4]=colorARGB;
		vp+=5;
	}

	return 0;
}

int gxtkGraphics::DrawPoly2( Array<Float> verts,gxtkSurface *surface,int srcx,int srcy ){

	int n=verts.Length()/4;
	if( n<1 || n>MAX_VERTS ) return 0;
		
	float *vp=Begin( n,n,surface );
	
	for( int i=0;i<n;++i ){
		int j=i*4;
		if( tformed ){
			vp[0]=verts[j] * ix + verts[j+1] * jx + tx;
			vp[1]=verts[j] * iy + verts[j+1] * jy + ty;
		}else{
			vp[0]=verts[j];
			vp[1]=verts[j+1];
		}
		vp[2]=(srcx+verts[j+2])*surface->uscale;
		vp[3]=(srcy+verts[j+3])*surface->vscale;
		(int&)vp[4]=colorARGB;
		vp+=5;
	}
	
	return 0;
}

int gxtkGraphics::DrawSurface( gxtkSurface *surf,float x,float y ){
	
	float w=surf->Width();
	float h=surf->Height();
	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;
	float u0=0,u1=w*surf->uscale;
	float v0=0,v1=h*surf->vscale;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,surf );
	
	vp[0 ]=x0;vp[1 ]=y0;vp[2 ]=u0;vp[3 ]=v0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;vp[7 ]=u1;vp[8 ]=v0;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;vp[12]=u1;vp[13]=v1;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;vp[17]=u0;vp[18]=v1;(int&)vp[19]=colorARGB;
	
	return 0;
}

int gxtkGraphics::DrawSurface2( gxtkSurface *surf,float x,float y,int srcx,int srcy,int srcw,int srch ){
	
	float w=srcw;
	float h=srch;
	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;
	float u0=srcx*surf->uscale,u1=(srcx+srcw)*surf->uscale;
	float v0=srcy*surf->vscale,v1=(srcy+srch)*surf->vscale;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,surf );
	
	vp[0 ]=x0;vp[1 ]=y0;vp[2 ]=u0;vp[3 ]=v0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;vp[7 ]=u1;vp[8 ]=v0;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;vp[12]=u1;vp[13]=v1;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;vp[17]=u0;vp[18]=v1;(int&)vp[19]=colorARGB;
	
	return 0;
}
	
int gxtkGraphics::ReadPixels( Array<int> pixels,int x,int y,int width,int height,int offset,int pitch ){

	Flush();

	unsigned *p=(unsigned*)malloc(width*height*4);

	glReadPixels( x,this->height-y-height,width,height,GL_BGRA,GL_UNSIGNED_BYTE,p );
	
	for( int py=0;py<height;++py ){
		memcpy( &pixels[offset+py*pitch],&p[(height-py-1)*width],width*4 );
	}
	
	free( p );
	
	return 0;
}

int gxtkGraphics::WritePixels2( gxtkSurface *surface,Array<int> pixels,int x,int y,int width,int height,int offset,int pitch ){

	surface->SetSubData( x,y,width,height,(unsigned*)&pixels[offset],pitch );
	
	return 0;
}

//***** gxtkSurface *****

gxtkSurface::gxtkSurface():data(0),width(0),height(0),depth(0),format(0),seq(-1),texture(0),uscale(0),vscale(0){
}

gxtkSurface::~gxtkSurface(){
	Discard();
}

int gxtkSurface::Discard(){
	if( seq==glfwGraphicsSeq ){
		glDeleteTextures( 1,&texture );
		seq=-1;
	}
	if( data ){
		free( data );
		data=0;
	}
	return 0;
}

int gxtkSurface::Width(){
	return width;
}

int gxtkSurface::Height(){
	return height;
}

int gxtkSurface::Loaded(){
	return 1;
}

//Careful! Can't call any GL here as it may be executing off-thread.
//
void gxtkSurface::SetData( unsigned char *data,int width,int height,int depth ){

	this->data=data;
	this->width=width;
	this->height=height;
	this->depth=depth;
	
	unsigned char *p=data;
	int n=width*height;
	
	switch( depth ){
	case 1:
		format=GL_LUMINANCE;
		break;
	case 2:
		format=GL_LUMINANCE_ALPHA;
		if( data ){
			while( n-- ){	//premultiply alpha
				p[0]=p[0]*p[1]/255;
				p+=2;
			}
		}
		break;
	case 3:
		format=GL_RGB;
		break;
	case 4:
		format=GL_RGBA;
		if( data ){
			while( n-- ){	//premultiply alpha
				p[0]=p[0]*p[3]/255;
				p[1]=p[1]*p[3]/255;
				p[2]=p[2]*p[3]/255;
				p+=4;
			}
		}
		break;
	}
}

void gxtkSurface::SetSubData( int x,int y,int w,int h,unsigned *src,int pitch ){
	if( format!=GL_RGBA ) return;
	
	if( !data ) data=(unsigned char*)malloc( width*height*4 );
	
	unsigned *dst=(unsigned*)data+y*width+x;
	
	for( int py=0;py<h;++py ){
		unsigned *d=dst+py*width;
		unsigned *s=src+py*pitch;
		for( int px=0;px<w;++px ){
			unsigned p=*s++;
			unsigned a=p>>24;
			*d++=(a<<24) | ((p>>0&0xff)*a/255<<16) | ((p>>8&0xff)*a/255<<8) | ((p>>16&0xff)*a/255);
		}
	}
	
	if( seq==glfwGraphicsSeq ){
		glBindTexture( GL_TEXTURE_2D,texture );
		glPixelStorei( GL_UNPACK_ALIGNMENT,1 );
		if( width==pitch ){
			glTexSubImage2D( GL_TEXTURE_2D,0,x,y,w,h,format,GL_UNSIGNED_BYTE,dst );
		}else{
			for( int py=0;py<h;++py ){
				glTexSubImage2D( GL_TEXTURE_2D,0,x,y+py,w,1,format,GL_UNSIGNED_BYTE,dst+py*width );
			}
		}
	}
}

void gxtkSurface::Bind(){

	if( !glfwGraphicsSeq ) return;
	
	if( seq==glfwGraphicsSeq ){
		glBindTexture( GL_TEXTURE_2D,texture );
		return;
	}
	
	seq=glfwGraphicsSeq;
	
	glGenTextures( 1,&texture );
	glBindTexture( GL_TEXTURE_2D,texture );
	
	if( CFG_MOJO_IMAGE_FILTERING_ENABLED ){
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
	}else{
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST );
	}

	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE );

	int texwidth=width;
	int texheight=height;
	
	glTexImage2D( GL_TEXTURE_2D,0,format,texwidth,texheight,0,format,GL_UNSIGNED_BYTE,0 );
	if( glGetError()!=GL_NO_ERROR ){
		texwidth=Pow2Size( width );
		texheight=Pow2Size( height );
		glTexImage2D( GL_TEXTURE_2D,0,format,texwidth,texheight,0,format,GL_UNSIGNED_BYTE,0 );
	}
	
	uscale=1.0/texwidth;
	vscale=1.0/texheight;
	
	if( data ){
		glPixelStorei( GL_UNPACK_ALIGNMENT,1 );
		glTexSubImage2D( GL_TEXTURE_2D,0,0,0,width,height,format,GL_UNSIGNED_BYTE,data );
	}
}

void gxtkSurface::OnUnsafeLoadComplete(){
	Bind();
}

bool gxtkGraphics::LoadSurface__UNSAFE__( gxtkSurface *surface,String path ){

	int width,height,depth;
	unsigned char *data=BBGlfwGame::GlfwGame()->LoadImageData( path,&width,&height,&depth );
	if( !data ) return false;
	
	surface->SetData( data,width,height,depth );
	return true;
}

gxtkSurface *gxtkGraphics::LoadSurface( String path ){
	gxtkSurface *surf=new gxtkSurface();
	if( !LoadSurface__UNSAFE__( surf,path ) ) return 0;
	surf->Bind();
	return surf;
}

gxtkSurface *gxtkGraphics::CreateSurface( int width,int height ){
	gxtkSurface *surf=new gxtkSurface();
	surf->SetData( 0,width,height,4 );
	surf->Bind();
	return surf;
}

//***** gxtkAudio.h *****

class gxtkSample;

class gxtkChannel{
public:
	ALuint source;
	gxtkSample *sample;
	int flags;
	int state;
	
	int AL_Source();
};

class gxtkAudio : public Object{
public:
	static gxtkAudio *audio;
	
	ALCdevice *alcDevice;
	ALCcontext *alcContext;
	gxtkChannel channels[33];

	gxtkAudio();

	virtual void mark();

	//***** GXTK API *****
	virtual int Suspend();
	virtual int Resume();

	virtual gxtkSample *LoadSample( String path );
	virtual bool LoadSample__UNSAFE__( gxtkSample *sample,String path );
	
	virtual int PlaySample( gxtkSample *sample,int channel,int flags );

	virtual int StopChannel( int channel );
	virtual int PauseChannel( int channel );
	virtual int ResumeChannel( int channel );
	virtual int ChannelState( int channel );
	virtual int SetVolume( int channel,float volume );
	virtual int SetPan( int channel,float pan );
	virtual int SetRate( int channel,float rate );
	
	virtual int PlayMusic( String path,int flags );
	virtual int StopMusic();
	virtual int PauseMusic();
	virtual int ResumeMusic();
	virtual int MusicState();
	virtual int SetMusicVolume( float volume );
};

class gxtkSample : public Object{
public:
	ALuint al_buffer;

	gxtkSample();
	gxtkSample( ALuint buf );
	~gxtkSample();
	
	void SetBuffer( ALuint buf );
	
	//***** GXTK API *****
	virtual int Discard();
};

//***** gxtkAudio.cpp *****

gxtkAudio *gxtkAudio::audio;

static std::vector<ALuint> discarded;

static void FlushDiscarded(){

	if( !discarded.size() ) return;
	
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&gxtkAudio::audio->channels[i];
		if( chan->state ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_STOPPED ) alSourcei( chan->source,AL_BUFFER,0 );
		}
	}
	
	std::vector<ALuint> out;
	
	for( int i=0;i<discarded.size();++i ){
		ALuint buf=discarded[i];
		alDeleteBuffers( 1,&buf );
		ALenum err=alGetError();
		if( err==AL_NO_ERROR ){
//			printf( "alDeleteBuffers OK!\n" );fflush( stdout );
		}else{
//			printf( "alDeleteBuffers failed...\n" );fflush( stdout );
			out.push_back( buf );
		}
	}
	discarded=out;
}

int gxtkChannel::AL_Source(){
	if( source ) return source;

	/*	
	static int n;
	if( ++n<17 ){
		alGetError();
		alGenSources( 1,&source );
		if( alGetError()==AL_NO_ERROR ) return source;
	}
	*/
	
	alGetError();
	alGenSources( 1,&source );
	if( alGetError()==AL_NO_ERROR ) return source;
	
	//couldn't create source...steal a free source...?
	//
	source=0;
	for( int i=0;i<32;++i ){
		gxtkChannel *chan=&gxtkAudio::audio->channels[i];
		if( !chan->source || gxtkAudio::audio->ChannelState( i ) ) continue;
//		puts( "Stealing source!" );
		source=chan->source;
		chan->source=0;
		break;
	}
	return source;
}

gxtkAudio::gxtkAudio(){

	audio=this;
	
	alcDevice=alcOpenDevice( 0 );
	if( !alcDevice ){
		alcDevice=alcOpenDevice( "Generic Hardware" );
		if( !alcDevice ) alcDevice=alcOpenDevice( "Generic Software" );
	}

	if( alcDevice ){
		if( alcContext=alcCreateContext( alcDevice,0 ) ){
			if( alcMakeContextCurrent( alcContext ) ){
				//alc all go!
			}else{
				bbPrint( "OpenAl error: alcMakeContextCurrent failed" );
			}
		}else{
			bbPrint( "OpenAl error: alcCreateContext failed" );
		}
	}else{
		bbPrint( "OpenAl error: alcOpenDevice failed" );
	}

	alDistanceModel( AL_NONE );
	
	memset( channels,0,sizeof(channels) );

	channels[32].AL_Source();
}

void gxtkAudio::mark(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state!=0 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state!=AL_STOPPED ) gc_mark( chan->sample );
		}
	}
}

int gxtkAudio::Suspend(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state==1 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_PLAYING ) alSourcePause( chan->source );
		}
	}
	return 0;
}

int gxtkAudio::Resume(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state==1 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_PAUSED ) alSourcePlay( chan->source );
		}
	}
	return 0;
}

bool gxtkAudio::LoadSample__UNSAFE__( gxtkSample *sample,String path ){

	int length=0;
	int channels=0;
	int format=0;
	int hertz=0;
	unsigned char *data=BBGlfwGame::GlfwGame()->LoadAudioData( path,&length,&channels,&format,&hertz );
	if( !data ) return false;
	
	int al_format=0;
	if( format==1 && channels==1 ){
		al_format=AL_FORMAT_MONO8;
	}else if( format==1 && channels==2 ){
		al_format=AL_FORMAT_STEREO8;
	}else if( format==2 && channels==1 ){
		al_format=AL_FORMAT_MONO16;
	}else if( format==2 && channels==2 ){
		al_format=AL_FORMAT_STEREO16;
	}
	
	int size=length*channels*format;
	
	ALuint al_buffer;
	alGenBuffers( 1,&al_buffer );
	alBufferData( al_buffer,al_format,data,size,hertz );
	free( data );
	
	sample->SetBuffer( al_buffer );
	return true;
}

gxtkSample *gxtkAudio::LoadSample( String path ){
	FlushDiscarded();
	gxtkSample *sample=new gxtkSample();
	if( !LoadSample__UNSAFE__( sample,path ) ) return 0;
	return sample;
}

int gxtkAudio::PlaySample( gxtkSample *sample,int channel,int flags ){

	FlushDiscarded();
	
	gxtkChannel *chan=&channels[channel];
	
	if( !chan->AL_Source() ) return -1;
	
	alSourceStop( chan->source );
	alSourcei( chan->source,AL_BUFFER,sample->al_buffer );
	alSourcei( chan->source,AL_LOOPING,flags ? 1 : 0 );
	alSourcePlay( chan->source );
	
	gc_assign( chan->sample,sample );

	chan->flags=flags;
	chan->state=1;

	return 0;
}

int gxtkAudio::StopChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state!=0 ){
		alSourceStop( chan->source );
		chan->state=0;
	}
	return 0;
}

int gxtkAudio::PauseChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state==1 ){
		int state=0;
		alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
		if( state==AL_STOPPED ){
			chan->state=0;
		}else{
			alSourcePause( chan->source );
			chan->state=2;
		}
	}
	return 0;
}

int gxtkAudio::ResumeChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state==2 ){
		alSourcePlay( chan->source );
		chan->state=1;
	}
	return 0;
}

int gxtkAudio::ChannelState( int channel ){
	gxtkChannel *chan=&channels[channel];
	
	if( chan->state==1 ){
		int state=0;
		alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
		if( state==AL_STOPPED ) chan->state=0;
	}
	return chan->state;
}

int gxtkAudio::SetVolume( int channel,float volume ){
	gxtkChannel *chan=&channels[channel];

	alSourcef( chan->AL_Source(),AL_GAIN,volume );
	return 0;
}

int gxtkAudio::SetPan( int channel,float pan ){
	gxtkChannel *chan=&channels[channel];
	
	float x=sinf( pan ),y=0,z=-cosf( pan );
	alSource3f( chan->AL_Source(),AL_POSITION,x,y,z );
	return 0;
}

int gxtkAudio::SetRate( int channel,float rate ){
	gxtkChannel *chan=&channels[channel];

	alSourcef( chan->AL_Source(),AL_PITCH,rate );
	return 0;
}

int gxtkAudio::PlayMusic( String path,int flags ){
	StopMusic();
	
	gxtkSample *music=LoadSample( path );
	if( !music ) return -1;
	
	PlaySample( music,32,flags );
	return 0;
}

int gxtkAudio::StopMusic(){
	StopChannel( 32 );
	return 0;
}

int gxtkAudio::PauseMusic(){
	PauseChannel( 32 );
	return 0;
}

int gxtkAudio::ResumeMusic(){
	ResumeChannel( 32 );
	return 0;
}

int gxtkAudio::MusicState(){
	return ChannelState( 32 );
}

int gxtkAudio::SetMusicVolume( float volume ){
	SetVolume( 32,volume );
	return 0;
}

gxtkSample::gxtkSample():
al_buffer(0){
}

gxtkSample::gxtkSample( ALuint buf ):
al_buffer(buf){
}

gxtkSample::~gxtkSample(){
	puts( "Discarding sample" );
	Discard();
}

void gxtkSample::SetBuffer( ALuint buf ){
	al_buffer=buf;
}

int gxtkSample::Discard(){
	if( al_buffer ){
		discarded.push_back( al_buffer );
		al_buffer=0;
	}
	return 0;
}


// ***** thread.h *****

#if __cplusplus_winrt

using namespace Windows::System::Threading;

#endif

class BBThread : public Object{
public:
	Object *result;
	
	BBThread();
	
	virtual void Start();
	virtual bool IsRunning();
	
	virtual Object *Result();
	virtual void SetResult( Object *result );
	
	static  String Strdup( const String &str );
	
	virtual void Run__UNSAFE__();
	
	
private:

	enum{
		INIT=0,
		RUNNING=1,
		FINISHED=2
	};

	
	int _state;
	Object *_result;
	
#if __cplusplus_winrt

	friend class Launcher;

	class Launcher{
	
		friend class BBThread;
		BBThread *_thread;
		
		Launcher( BBThread *thread ):_thread(thread){
		}
		
		public:
		
		void operator()( IAsyncAction ^operation ){
			_thread->Run__UNSAFE__();
			_thread->_state=FINISHED;
		} 
	};
	
#elif _WIN32

	static DWORD WINAPI run( void *p );
	
#else

	static void *run( void *p );
	
#endif

};

// ***** thread.cpp *****

BBThread::BBThread():_state( INIT ),_result( 0 ){
}

bool BBThread::IsRunning(){
	return _state==RUNNING;
}

Object *BBThread::Result(){
	return _result;
}

void BBThread::SetResult( Object *result ){
	_result=result;
}

String BBThread::Strdup( const String &str ){
	return str.Copy();
}

void BBThread::Run__UNSAFE__(){
}

#if __cplusplus_winrt

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	Launcher launcher( this );
	
	auto handler=ref new WorkItemHandler( launcher );
	
	ThreadPool::RunAsync( handler );
}

#elif _WIN32

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	DWORD _id;
	HANDLE _handle;

	if( _handle=CreateThread( 0,0,run,this,0,&_id ) ){
		CloseHandle( _handle );
		return;
	}
	
	puts( "CreateThread failed!" );
	exit( -1 );
}

DWORD WINAPI BBThread::run( void *p ){
	BBThread *thread=(BBThread*)p;

	thread->Run__UNSAFE__();
	
	thread->_state=FINISHED;
	return 0;
}

#else

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	pthread_t _handle;
	
	if( !pthread_create( &_handle,0,run,this ) ){
		pthread_detach( _handle );
		return;
	}
	
	puts( "pthread_create failed!" );
	exit( -1 );
}

void *BBThread::run( void *p ){
	BBThread *thread=(BBThread*)p;

	thread->Run__UNSAFE__();

	thread->_state=FINISHED;
	return 0;
}

#endif

// Stdcpp trans.system runtime.
//
// Placed into the public domain 24/02/2011.
// No warranty implied; use as your own risk.

#if _WIN32

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

typedef WCHAR OS_CHAR;
typedef struct _stat stat_t;

#define mkdir( X,Y ) _wmkdir( X )
#define rmdir _wrmdir
#define remove _wremove
#define rename _wrename
#define stat _wstat
#define _fopen _wfopen
#define putenv _wputenv
#define getenv _wgetenv
#define system _wsystem
#define chdir _wchdir
#define getcwd _wgetcwd
#define realpath(X,Y) _wfullpath( Y,X,PATH_MAX )	//Note: first args SWAPPED to be posix-like!
#define opendir _wopendir
#define readdir _wreaddir
#define closedir _wclosedir
#define DIR _WDIR
#define dirent _wdirent

#elif __APPLE__

typedef char OS_CHAR;
typedef struct stat stat_t;

#define _fopen fopen

#elif __linux

/*
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
*/

typedef char OS_CHAR;
typedef struct stat stat_t;

#define _fopen fopen

#endif

static String _appPath;
static Array<String> _appArgs;

static String::CString<char> C_STR( const String &t ){
	return t.ToCString<char>();
}

static String::CString<OS_CHAR> OS_STR( const String &t ){
	return t.ToCString<OS_CHAR>();
}

String HostOS(){
#if _WIN32
	return "winnt";
#elif __APPLE__
	return "macos";
#elif __linux
	return "linux";
#else
	return "";
#endif
}

String RealPath( String path ){
	std::vector<OS_CHAR> buf( PATH_MAX+1 );
	if( realpath( OS_STR( path ),&buf[0] ) ){}
	buf[buf.size()-1]=0;
	for( int i=0;i<PATH_MAX && buf[i];++i ){
		if( buf[i]=='\\' ) buf[i]='/';
		
	}
	return String( &buf[0] );
}

String AppPath(){

	if( _appPath.Length() ) return _appPath;
	
#if _WIN32

	OS_CHAR buf[PATH_MAX+1];
	GetModuleFileNameW( GetModuleHandleW(0),buf,PATH_MAX );
	buf[PATH_MAX]=0;
	_appPath=String( buf );
	
#elif __APPLE__

	char buf[PATH_MAX];
	uint32_t size=sizeof( buf );
	_NSGetExecutablePath( buf,&size );
	buf[PATH_MAX-1]=0;
	_appPath=String( buf );
	
#elif __linux

	char lnk[PATH_MAX],buf[PATH_MAX];
	pid_t pid=getpid();
	sprintf( lnk,"/proc/%i/exe",pid );
	int i=readlink( lnk,buf,PATH_MAX );
	if( i>0 && i<PATH_MAX ){
		buf[i]=0;
		_appPath=String( buf );
	}

#endif

	_appPath=RealPath( _appPath );
	return _appPath;
}

Array<String> AppArgs(){
	if( _appArgs.Length() ) return _appArgs;
	_appArgs=Array<String>( argc );
	for( int i=0;i<argc;++i ){
		_appArgs[i]=String( argv[i] );
	}
	return _appArgs;
}
	
int FileType( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return 0;
	switch( st.st_mode & S_IFMT ){
	case S_IFREG : return 1;
	case S_IFDIR : return 2;
	}
	return 0;
}

int FileSize( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return -1;
	return st.st_size;
}

int FileTime( String path ){
	stat_t st;
	if( stat( OS_STR(path),&st ) ) return -1;
	return st.st_mtime;
}

String LoadString( String path ){
	if( FILE *fp=_fopen( OS_STR(path),OS_STR("rb") ) ){
		String str=String::Load( fp );
		if( _str_load_err ){
			bbPrint( String( _str_load_err )+" in file: "+path );
		}
		fclose( fp );
		return str;
	}
	return "";
}
	
int SaveString( String str,String path ){
	if( FILE *fp=_fopen( OS_STR(path),OS_STR("wb") ) ){
		bool ok=str.Save( fp );
		fclose( fp );
		return ok ? 0 : -2;
	}else{
//		printf( "FOPEN 'wb' for SaveString '%s' failed\n",C_STR( path ) );
		fflush( stdout );
	}
	return -1;
}

Array<String> LoadDir( String path ){
	std::vector<String> files;
	
#if _WIN32

	WIN32_FIND_DATAW filedata;
	HANDLE handle=FindFirstFileW( OS_STR(path+"/*"),&filedata );
	if( handle!=INVALID_HANDLE_VALUE ){
		do{
			String f=filedata.cFileName;
			if( f=="." || f==".." ) continue;
			files.push_back( f );
		}while( FindNextFileW( handle,&filedata ) );
		FindClose( handle );
	}else{
//		printf( "FindFirstFileW for LoadDir(%s) failed\n",C_STR(path) );
		fflush( stdout );
	}
	
#else

	if( DIR *dir=opendir( OS_STR(path) ) ){
		while( dirent *ent=readdir( dir ) ){
			String f=ent->d_name;
			if( f=="." || f==".." ) continue;
			files.push_back( f );
		}
		closedir( dir );
	}else{
//		printf( "opendir for LoadDir(%s) failed\n",C_STR(path) );
		fflush( stdout );
	}

#endif

	return files.size() ? Array<String>( &files[0],files.size() ) : Array<String>();
}
	
int CopyFile( String srcpath,String dstpath ){

#if _WIN32

	if( CopyFileW( OS_STR(srcpath),OS_STR(dstpath),FALSE ) ) return 1;
	return 0;
	
#elif __APPLE__

	// Would like to use COPY_ALL here, but it breaks trans on MacOS - produces weird 'pch out of date' error with copied projects.
	//
	// Ranlib strikes back!
	//
	if( copyfile( OS_STR(srcpath),OS_STR(dstpath),0,COPYFILE_DATA )>=0 ) return 1;
	return 0;
	
#else

	int err=-1;
	if( FILE *srcp=_fopen( OS_STR( srcpath ),OS_STR( "rb" ) ) ){
		err=-2;
		if( FILE *dstp=_fopen( OS_STR( dstpath ),OS_STR( "wb" ) ) ){
			err=0;
			char buf[1024];
			while( int n=fread( buf,1,1024,srcp ) ){
				if( fwrite( buf,1,n,dstp )!=n ){
					err=-3;
					break;
				}
			}
			fclose( dstp );
		}else{
//			printf( "FOPEN 'wb' for CopyFile(%s,%s) failed\n",C_STR(srcpath),C_STR(dstpath) );
			fflush( stdout );
		}
		fclose( srcp );
	}else{
//		printf( "FOPEN 'rb' for CopyFile(%s,%s) failed\n",C_STR(srcpath),C_STR(dstpath) );
		fflush( stdout );
	}
	return err==0;
	
#endif
}

int ChangeDir( String path ){
	return chdir( OS_STR(path) );
}

String CurrentDir(){
	std::vector<OS_CHAR> buf( PATH_MAX+1 );
	if( getcwd( &buf[0],buf.size() ) ){}
	buf[buf.size()-1]=0;
	return String( &buf[0] );
}

int CreateDir( String path ){
	mkdir( OS_STR( path ),0777 );
	return FileType(path)==2;
}

int DeleteDir( String path ){
	rmdir( OS_STR(path) );
	return FileType(path)==0;
}

int DeleteFile( String path ){
	remove( OS_STR(path) );
	return FileType(path)==0;
}

int SetEnv( String name,String value ){
#if _WIN32
	return putenv( OS_STR( name+"="+value ) );
#else
	if( value.Length() ) return setenv( OS_STR( name ),OS_STR( value ),1 );
	unsetenv( OS_STR( name ) );
	return 0;
#endif
}

String GetEnv( String name ){
	if( OS_CHAR *p=getenv( OS_STR(name) ) ) return String( p );
	return "";
}

int Execute( String cmd ){

#if _WIN32

	cmd=String("cmd /S /C \"")+cmd+"\"";

	PROCESS_INFORMATION pi={0};
	STARTUPINFOW si={sizeof(si)};

	if( !CreateProcessW( 0,(WCHAR*)(const OS_CHAR*)OS_STR(cmd),0,0,1,CREATE_DEFAULT_ERROR_MODE,0,0,&si,&pi ) ) return -1;

	WaitForSingleObject( pi.hProcess,INFINITE );

	int res=GetExitCodeProcess( pi.hProcess,(DWORD*)&res ) ? res : -1;

	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );

	return res;

#else

	return system( OS_STR(cmd) );

#endif
}

int ExitApp( int retcode ){
	exit( retcode );
	return 0;
}

/*
Copyright (c) 2011 Steve Revill and Shane Woolcock
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifdef _WIN32
#include <windows.h>
#include <Shellapi.h>
#include <iostream>
#endif

#include <string.h>
#include <time.h>

extern gxtkAudio *bb_audio_device;
extern gxtkGraphics *bb_graphics_device;

float diddy_mouseWheel = 0.0f;



float diddy_mouseZ() {
	float ret = glfwGetMouseWheel() - diddy_mouseWheel;
	diddy_mouseWheel = glfwGetMouseWheel();
	return ret;
}

class diddy
{
	public:
	
	// Returns an empty string if dialog is cancelled
	static String openfilename() {
		#ifdef _WIN32
		char *filter = "All Files (*.*)\0*.*\0";
		HWND owner = NULL;
		OPENFILENAME ofn;
		char fileName[MAX_PATH] = "";
		ZeroMemory(&ofn, sizeof(ofn));

		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.hwndOwner = owner;
		ofn.lpstrFilter = filter;
		ofn.lpstrFile = fileName;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		ofn.lpstrDefExt = "";

		String fileNameStr;

		if ( GetOpenFileName(&ofn) )
			fileNameStr = fileName;

		return fileNameStr;
		#endif
		#ifdef linux
		return "";
		#endif
	}
	
	static String savefilename() {
		#ifdef _WIN32
		char *filter = "All Files (*.*)\0*.*\0";
		HWND owner = NULL;
		OPENFILENAME ofn;
		char fileName[MAX_PATH] = "";
		ZeroMemory(&ofn, sizeof(ofn));

		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.hwndOwner = owner;
		ofn.lpstrFilter = filter;
		ofn.lpstrFile = fileName;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		ofn.lpstrDefExt = "";

		String fileNameStr;

		if ( GetSaveFileNameA(&ofn) )
			fileNameStr = fileName;

		return fileNameStr;
		#endif
		#ifdef linux
		return "";
		#endif
	}
	
	static float mouseZ()
	{
		return diddy_mouseZ();
	}
	
	static void mouseZInit()
	{
		return;
	}
	
	// only accurate to 1 second 
	static int systemMillisecs() {
		time_t seconds;
		seconds = time (NULL);
		return seconds * 1000;
	}
	
	static void setGraphics(int w, int h)
	{
		glfwSetWindowSize(w, h);
		GLFWvidmode desktopMode;
		glfwGetDesktopMode( &desktopMode );
		glfwSetWindowPos( (desktopMode.Width-w)/2,(desktopMode.Height-h)/2 );
	}
	
	static void setMouse(int x, int y)
	{
		glfwSetMousePos(x, y);
	}
	
	static void showKeyboard()
	{
	}
	static void launchBrowser(String address, String windowName)
	{
		#ifdef _WIN32
		LPCSTR addressStr = address.ToCString<char>();
		ShellExecute(HWND_DESKTOP, "open", addressStr, NULL, NULL, SW_SHOWNORMAL);
		#endif
	}
	static void launchEmail(String email, String subject, String text)
	{
		#ifdef _WIN32
		String tmp = "mailto:";
		tmp+=email;
		tmp+="&subject=";
		tmp+=subject;
		tmp+="&body=";
		tmp+=text;
		LPCSTR addressStr = tmp.ToCString<char>();
		ShellExecute(HWND_DESKTOP, "open", addressStr, NULL, NULL, SW_SHOWNORMAL);
		#endif
	}

	static void startVibrate(int millisecs)
	{
	}
	static void stopVibrate()
	{
	}
	
	static void startGps()
	{
	}
	static String getLatitiude()
	{
		return "";
	}
	static String getLongitude()
	{
		return "";
	}
	static void showAlertDialog(String title, String message)
	{
	}
	static String getInputString()
	{
		return "";
	}

	static int seekMusic(int timeMillis)
	{
		gxtkChannel *chan = &(bb_audio_device->channels[32]);
		if(chan && chan->state==1)
		{
			alSourcef(chan->source, AL_SEC_OFFSET, (float)(timeMillis / 1000.0));
		}
		// TODO: check it worked
		return 1;
	}
};

class c_App;
class c_MyApp;
class c_GameDelegate;
class c_Image;
class c_GraphicsContext;
class c_Frame;
class c_InputDevice;
class c_JoyState;
class c_DisplayMode;
class c_Map;
class c_IntMap;
class c_Stack;
class c_Node;
class c_BBGameEvent;
class c_NinePatchImage;
class c_Template;
class c_List;
class c_Node2;
class c_HeadNode;
class c_Enumerator;
class c_Setting;
class c_Map2;
class c_StringMap;
class c_Node3;
class c_Gadget;
class c_ViewGadget;
class c_ContainerGadget;
class c_WindowGadget;
class c_Event;
class c_Browser;
class c_ButtonGadget;
class c_BrowserButton;
class c_List2;
class c_Node4;
class c_HeadNode2;
class c_Enumerator2;
class c_List3;
class c_Node5;
class c_HeadNode3;
class c_BackwardsList;
class c_BackwardsEnumerator;
class c_Project;
class c_Patch;
class c_Spark;
class c_List4;
class c_Node6;
class c_HeadNode4;
class c_Enumerator3;
class c_Wire;
class c_Box;
class c_List5;
class c_Node7;
class c_HeadNode5;
class c_Enumerator4;
class c_List6;
class c_Node8;
class c_HeadNode6;
class c_Enumerator5;
class c_Rules;
class c_Map3;
class c_IntMap2;
class c_Node9;
class c_View;
class c_ScissorBox;
class c_List7;
class c_Node10;
class c_HeadNode7;
class c_TabGadget;
class c_List8;
class c_Node11;
class c_HeadNode8;
class c_Enumerator6;
class c_BackwardsList2;
class c_BackwardsEnumerator2;
class c_GadgetEvent;
class c_Panel;
class c_MapValues;
class c_ValueEnumerator;
class c_Slider;
class c_NumberBox;
class c_DropList;
class c_CheckBox;
class c_RuleTable;
class c_ViewBox;
class c_List9;
class c_Node12;
class c_HeadNode9;
class c_Tray;
class c_App : public Object{
	public:
	c_App();
	c_App* m_new();
	int p_OnResize();
	virtual int p_OnCreate();
	int p_OnSuspend();
	int p_OnResume();
	virtual int p_OnUpdate();
	int p_OnLoading();
	virtual int p_OnRender();
	int p_OnClose();
	int p_OnBack();
	void mark();
	String debug();
};
String dbg_type(c_App**p){return "App";}
class c_MyApp : public c_App{
	public:
	c_WindowGadget* m_window;
	c_Browser* m_browser;
	c_Project* m__project;
	c_MyApp();
	c_MyApp* m_new();
	int p_OnCreate();
	c_Project* p_project();
	void p_project2(c_Project*);
	c_Patch* p_patch();
	int p_OnUpdate();
	int p_OnRender();
	c_Panel* p_panel();
	void mark();
	String debug();
};
String dbg_type(c_MyApp**p){return "MyApp";}
extern c_App* bb_app__app;
class c_GameDelegate : public BBGameDelegate{
	public:
	gxtkGraphics* m__graphics;
	gxtkAudio* m__audio;
	c_InputDevice* m__input;
	c_GameDelegate();
	c_GameDelegate* m_new();
	void StartGame();
	void SuspendGame();
	void ResumeGame();
	void UpdateGame();
	void RenderGame();
	void KeyEvent(int,int);
	void MouseEvent(int,int,Float,Float);
	void TouchEvent(int,int,Float,Float);
	void MotionEvent(int,int,Float,Float,Float);
	void DiscardGraphics();
	void mark();
	String debug();
};
String dbg_type(c_GameDelegate**p){return "GameDelegate";}
extern c_GameDelegate* bb_app__delegate;
extern BBGame* bb_app__game;
extern c_MyApp* bb_main_APP;
int bbMain();
extern gxtkGraphics* bb_graphics_device;
int bb_graphics_SetGraphicsDevice(gxtkGraphics*);
class c_Image : public Object{
	public:
	gxtkSurface* m_surface;
	int m_width;
	int m_height;
	Array<c_Frame* > m_frames;
	int m_flags;
	Float m_tx;
	Float m_ty;
	c_Image* m_source;
	c_Image();
	static int m_DefaultFlags;
	c_Image* m_new();
	int p_SetHandle(Float,Float);
	int p_ApplyFlags(int);
	c_Image* p_Init(gxtkSurface*,int,int);
	c_Image* p_Init2(gxtkSurface*,int,int,int,int,int,int,c_Image*,int,int,int,int);
	Float p_HandleX();
	Float p_HandleY();
	int p_Width();
	int p_Height();
	c_Image* p_GrabImage(int,int,int,int,int,int);
	int p_Frames();
	void mark();
	String debug();
};
String dbg_type(c_Image**p){return "Image";}
class c_GraphicsContext : public Object{
	public:
	c_Image* m_defaultFont;
	c_Image* m_font;
	int m_firstChar;
	int m_matrixSp;
	Float m_ix;
	Float m_iy;
	Float m_jx;
	Float m_jy;
	Float m_tx;
	Float m_ty;
	int m_tformed;
	int m_matDirty;
	Float m_color_r;
	Float m_color_g;
	Float m_color_b;
	Float m_alpha;
	int m_blend;
	Float m_scissor_x;
	Float m_scissor_y;
	Float m_scissor_width;
	Float m_scissor_height;
	Array<Float > m_matrixStack;
	c_GraphicsContext();
	c_GraphicsContext* m_new();
	int p_Validate();
	void mark();
	String debug();
};
String dbg_type(c_GraphicsContext**p){return "GraphicsContext";}
extern c_GraphicsContext* bb_graphics_context;
String bb_data_FixDataPath(String);
class c_Frame : public Object{
	public:
	int m_x;
	int m_y;
	c_Frame();
	c_Frame* m_new(int,int);
	c_Frame* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Frame**p){return "Frame";}
c_Image* bb_graphics_LoadImage(String,int,int);
c_Image* bb_graphics_LoadImage2(String,int,int,int,int);
int bb_graphics_SetFont(c_Image*,int);
extern gxtkAudio* bb_audio_device;
int bb_audio_SetAudioDevice(gxtkAudio*);
class c_InputDevice : public Object{
	public:
	Array<c_JoyState* > m__joyStates;
	Array<bool > m__keyDown;
	int m__keyHitPut;
	Array<int > m__keyHitQueue;
	Array<int > m__keyHit;
	int m__charGet;
	int m__charPut;
	Array<int > m__charQueue;
	Float m__mouseX;
	Float m__mouseY;
	Array<Float > m__touchX;
	Array<Float > m__touchY;
	Float m__accelX;
	Float m__accelY;
	Float m__accelZ;
	c_InputDevice();
	c_InputDevice* m_new();
	void p_PutKeyHit(int);
	void p_BeginUpdate();
	void p_EndUpdate();
	void p_KeyEvent(int,int);
	void p_MouseEvent(int,int,Float,Float);
	void p_TouchEvent(int,int,Float,Float);
	void p_MotionEvent(int,int,Float,Float,Float);
	Float p_MouseX();
	Float p_MouseY();
	bool p_KeyDown(int);
	int p_KeyHit(int);
	void mark();
	String debug();
};
String dbg_type(c_InputDevice**p){return "InputDevice";}
class c_JoyState : public Object{
	public:
	Array<Float > m_joyx;
	Array<Float > m_joyy;
	Array<Float > m_joyz;
	Array<bool > m_buttons;
	c_JoyState();
	c_JoyState* m_new();
	void mark();
	String debug();
};
String dbg_type(c_JoyState**p){return "JoyState";}
extern c_InputDevice* bb_input_device;
int bb_input_SetInputDevice(c_InputDevice*);
extern int bb_app__devWidth;
extern int bb_app__devHeight;
void bb_app_ValidateDeviceWindow(bool);
class c_DisplayMode : public Object{
	public:
	int m__width;
	int m__height;
	c_DisplayMode();
	c_DisplayMode* m_new(int,int);
	c_DisplayMode* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_DisplayMode**p){return "DisplayMode";}
class c_Map : public Object{
	public:
	c_Node* m_root;
	c_Map();
	c_Map* m_new();
	virtual int p_Compare(int,int)=0;
	c_Node* p_FindNode(int);
	bool p_Contains(int);
	int p_RotateLeft(c_Node*);
	int p_RotateRight(c_Node*);
	int p_InsertFixup(c_Node*);
	bool p_Set(int,c_DisplayMode*);
	bool p_Insert(int,c_DisplayMode*);
	void mark();
	String debug();
};
String dbg_type(c_Map**p){return "Map";}
class c_IntMap : public c_Map{
	public:
	c_IntMap();
	c_IntMap* m_new();
	int p_Compare(int,int);
	void mark();
	String debug();
};
String dbg_type(c_IntMap**p){return "IntMap";}
class c_Stack : public Object{
	public:
	Array<c_DisplayMode* > m_data;
	int m_length;
	c_Stack();
	c_Stack* m_new();
	c_Stack* m_new2(Array<c_DisplayMode* >);
	void p_Push(c_DisplayMode*);
	void p_Push2(Array<c_DisplayMode* >,int,int);
	void p_Push3(Array<c_DisplayMode* >,int);
	Array<c_DisplayMode* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack**p){return "Stack";}
class c_Node : public Object{
	public:
	int m_key;
	c_Node* m_right;
	c_Node* m_left;
	c_DisplayMode* m_value;
	int m_color;
	c_Node* m_parent;
	c_Node();
	c_Node* m_new(int,c_DisplayMode*,int,c_Node*);
	c_Node* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node**p){return "Node";}
extern Array<c_DisplayMode* > bb_app__displayModes;
extern c_DisplayMode* bb_app__desktopMode;
int bb_app_DeviceWidth();
int bb_app_DeviceHeight();
void bb_app_EnumDisplayModes();
extern gxtkGraphics* bb_graphics_renderDevice;
int bb_graphics_SetMatrix(Float,Float,Float,Float,Float,Float);
int bb_graphics_SetMatrix2(Array<Float >);
int bb_graphics_SetColor(Float,Float,Float);
int bb_graphics_SetAlpha(Float);
int bb_graphics_SetBlend(int);
int bb_graphics_SetScissor(Float,Float,Float,Float);
int bb_graphics_BeginRender();
int bb_graphics_EndRender();
class c_BBGameEvent : public Object{
	public:
	c_BBGameEvent();
	void mark();
	String debug();
};
String dbg_type(c_BBGameEvent**p){return "BBGameEvent";}
void bb_app_EndApp();
extern int bb_app__updateRate;
void bb_app_SetUpdateRate(int);
extern c_Image* bb_main_imgX;
extern c_Image* bb_main_imgO;
class c_NinePatchImage : public Object{
	public:
	Array<c_Image* > m_patches;
	int m_w0;
	int m_w2;
	int m_h0;
	int m_h2;
	int m_xSafe;
	int m_wSafe;
	int m_ySafe;
	int m_hSafe;
	int m_xOffset;
	int m_yOffset;
	int m_wOffset;
	int m_hOffset;
	c_NinePatchImage();
	c_NinePatchImage* m_new(c_Image*,Array<int >,Array<int >);
	c_NinePatchImage* m_new2();
	int p_minimumWidth();
	int p_minimumHeight();
	void p_Draw(int,int,int,int);
	void mark();
	String debug();
};
String dbg_type(c_NinePatchImage**p){return "NinePatchImage";}
extern c_NinePatchImage* bb_main_imgTab;
extern c_Image* bb_main_imgClose;
extern c_Image* bb_main_imgOpen;
extern c_Image* bb_main_imgSave;
extern c_Image* bb_main_imgNew;
class c_Template : public Object{
	public:
	String m_name;
	int m_ins;
	int m_outs;
	c_StringMap* m_settings;
	c_Template();
	c_Template* m_new(String,int,int);
	c_Template* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Template**p){return "Template";}
class c_List : public Object{
	public:
	c_Node2* m__head;
	c_List();
	c_List* m_new();
	c_Node2* p_AddLast(c_Template*);
	c_List* m_new2(Array<c_Template* >);
	c_Enumerator* p_ObjectEnumerator();
	void mark();
	String debug();
};
String dbg_type(c_List**p){return "List";}
class c_Node2 : public Object{
	public:
	c_Node2* m__succ;
	c_Node2* m__pred;
	c_Template* m__data;
	c_Node2();
	c_Node2* m_new(c_Node2*,c_Node2*,c_Template*);
	c_Node2* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node2**p){return "Node";}
class c_HeadNode : public c_Node2{
	public:
	c_HeadNode();
	c_HeadNode* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode**p){return "HeadNode";}
extern c_List* bb_template_templates;
void bb_template_AddTemplate(String,int,int);
class c_Enumerator : public Object{
	public:
	c_List* m__list;
	c_Node2* m__curr;
	c_Enumerator();
	c_Enumerator* m_new(c_List*);
	c_Enumerator* m_new2();
	bool p_HasNext();
	c_Template* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator**p){return "Enumerator";}
c_Template* bb_template__GetTemplate(String);
class c_Setting : public Object{
	public:
	String m_name;
	String m_kind;
	int m_value;
	c_Setting();
	c_Setting* m_new(String,String,int);
	c_Setting* m_new2();
	c_Setting* p_Copy();
	void mark();
	String debug();
};
String dbg_type(c_Setting**p){return "Setting";}
class c_Map2 : public Object{
	public:
	c_Node3* m_root;
	c_Map2();
	c_Map2* m_new();
	virtual int p_Compare2(String,String)=0;
	int p_RotateLeft2(c_Node3*);
	int p_RotateRight2(c_Node3*);
	int p_InsertFixup2(c_Node3*);
	bool p_Set2(String,c_Setting*);
	bool p_Insert2(String,c_Setting*);
	c_Node3* p_FindNode2(String);
	c_Setting* p_Get(String);
	c_MapValues* p_Values();
	c_Node3* p_FirstNode();
	void mark();
	String debug();
};
String dbg_type(c_Map2**p){return "Map";}
class c_StringMap : public c_Map2{
	public:
	c_StringMap();
	c_StringMap* m_new();
	int p_Compare2(String,String);
	void mark();
	String debug();
};
String dbg_type(c_StringMap**p){return "StringMap";}
class c_Node3 : public Object{
	public:
	String m_key;
	c_Node3* m_right;
	c_Node3* m_left;
	c_Setting* m_value;
	int m_color;
	c_Node3* m_parent;
	c_Node3();
	c_Node3* m_new(String,c_Setting*,int,c_Node3*);
	c_Node3* m_new2();
	c_Node3* p_NextNode();
	void mark();
	String debug();
};
String dbg_type(c_Node3**p){return "Node";}
void bb_template_AddSetting(String,String,String,int);
void bb_template_MakeTemplates();
class c_Gadget : public Object{
	public:
	int m_x;
	int m_y;
	int m_w;
	int m_h;
	c_ContainerGadget* m_parent;
	c_WindowGadget* m_window;
	bool m__enabled;
	c_Gadget();
	c_Gadget* m_new();
	virtual c_Gadget* p_HandleEvent(c_Event*)=0;
	virtual int p_xTranslate();
	int p__GlobalX(int);
	int p__LocalX(int);
	int p_GetLocalX(int,c_Gadget*);
	virtual int p_yTranslate();
	int p__GlobalY(int);
	int p__LocalY(int);
	int p_GetLocalY(int,c_Gadget*);
	bool p_enabled();
	void p_Disable();
	void p_Enable();
	virtual void p_OnRender();
	virtual void p_Render();
	virtual void p_HandleGadgetEvent(c_GadgetEvent*);
	void mark();
	String debug();
};
String dbg_type(c_Gadget**p){return "Gadget";}
class c_ViewGadget : public c_Gadget{
	public:
	int m_ox;
	int m_oy;
	c_ViewGadget();
	c_ViewGadget* m_new();
	virtual void p_OnRenderInterior();
	void p_Render();
	int p_xTranslate();
	int p_yTranslate();
	void mark();
	String debug();
};
String dbg_type(c_ViewGadget**p){return "ViewGadget";}
class c_ContainerGadget : public c_ViewGadget{
	public:
	c_List2* m_children;
	c_ContainerGadget();
	c_ContainerGadget* m_new(int,int,int,int);
	c_ContainerGadget* m_new2();
	void p__PassDownWindow();
	virtual void p_AddChild(c_Gadget*);
	c_Gadget* p_HandleEvent(c_Event*);
	void p_OnRender();
	void p_OnRenderInterior();
	void mark();
	String debug();
};
String dbg_type(c_ContainerGadget**p){return "ContainerGadget";}
class c_WindowGadget : public c_ContainerGadget{
	public:
	int m__mouseX;
	int m__mousePreviousX;
	int m__mouseY;
	int m__mousePreviousY;
	int m__mouseState;
	int m__mouseDragX;
	int m__mouseDragY;
	c_Gadget* m__destination;
	c_List3* m__events;
	int m__mousePreviousState;
	c_WindowGadget();
	c_WindowGadget* m_new(int,int,int,int);
	c_WindowGadget* m_new2();
	void p_AddChild(c_Gadget*);
	void p_Update();
	c_Gadget* p_HandleEvent(c_Event*);
	void mark();
	String debug();
};
String dbg_type(c_WindowGadget**p){return "WindowGadget";}
class c_Event : public Object{
	public:
	int m_id;
	c_WindowGadget* m_window;
	int m_x;
	int m_y;
	int m_dx;
	int m_dy;
	c_Gadget* m_destination;
	c_Event();
	static c_WindowGadget* m_globalWindow;
	c_Event* m_new(int,c_WindowGadget*);
	c_Event* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Event**p){return "Event";}
class c_Browser : public c_ContainerGadget{
	public:
	c_BrowserButton* m_openButton;
	c_BrowserButton* m_newButton;
	c_List8* m_tabs;
	c_Browser();
	c_Browser* m_new(int,int,int,int);
	c_Browser* m_new2();
	void p_OnRender();
	void p_OnRenderInterior();
	void p_RemoveTab(c_TabGadget*);
	void p_AddTab(String);
	void p_HandleGadgetEvent(c_GadgetEvent*);
	void mark();
	String debug();
};
String dbg_type(c_Browser**p){return "Browser";}
class c_ButtonGadget : public c_Gadget{
	public:
	int m_state;
	c_ButtonGadget();
	c_ButtonGadget* m_new(int,int);
	c_ButtonGadget* m_new2();
	c_Gadget* p_HandleEvent(c_Event*);
	void p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_ButtonGadget**p){return "ButtonGadget";}
class c_BrowserButton : public c_ButtonGadget{
	public:
	c_Image* m_image;
	c_BrowserButton();
	c_BrowserButton* m_new(int,int,c_Image*);
	c_BrowserButton* m_new2();
	void p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_BrowserButton**p){return "BrowserButton";}
class c_List2 : public Object{
	public:
	c_Node4* m__head;
	c_List2();
	c_List2* m_new();
	c_Node4* p_AddLast2(c_Gadget*);
	c_List2* m_new2(Array<c_Gadget* >);
	c_Enumerator2* p_ObjectEnumerator();
	c_BackwardsList* p_Backwards();
	bool p_IsEmpty();
	c_Gadget* p_Last();
	int p_Clear();
	bool p_Equals(c_Gadget*,c_Gadget*);
	int p_RemoveEach(c_Gadget*);
	void p_Remove(c_Gadget*);
	c_Gadget* p_RemoveLast();
	c_Node4* p_FindLast(c_Gadget*,c_Node4*);
	c_Node4* p_FindLast2(c_Gadget*);
	void p_RemoveLast2(c_Gadget*);
	c_Node4* p_AddFirst(c_Gadget*);
	c_Node4* p_Find(c_Gadget*,c_Node4*);
	c_Node4* p_Find2(c_Gadget*);
	c_Node4* p_InsertAfter(c_Gadget*,c_Gadget*);
	void mark();
	String debug();
};
String dbg_type(c_List2**p){return "List";}
class c_Node4 : public Object{
	public:
	c_Node4* m__succ;
	c_Node4* m__pred;
	c_Gadget* m__data;
	c_Node4();
	c_Node4* m_new(c_Node4*,c_Node4*,c_Gadget*);
	c_Node4* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node4**p){return "Node";}
class c_HeadNode2 : public c_Node4{
	public:
	c_HeadNode2();
	c_HeadNode2* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode2**p){return "HeadNode";}
class c_Enumerator2 : public Object{
	public:
	c_List2* m__list;
	c_Node4* m__curr;
	c_Enumerator2();
	c_Enumerator2* m_new(c_List2*);
	c_Enumerator2* m_new2();
	bool p_HasNext();
	c_Gadget* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator2**p){return "Enumerator";}
Float bb_input_MouseX();
Float bb_input_MouseY();
class c_List3 : public Object{
	public:
	c_Node5* m__head;
	c_List3();
	c_List3* m_new();
	c_Node5* p_AddLast3(c_Event*);
	c_List3* m_new2(Array<c_Event* >);
	int p_Count();
	bool p_IsEmpty();
	c_Event* p_RemoveFirst();
	bool p_Equals2(c_Event*,c_Event*);
	c_Node5* p_Find3(c_Event*,c_Node5*);
	c_Node5* p_Find4(c_Event*);
	void p_RemoveFirst2(c_Event*);
	void mark();
	String debug();
};
String dbg_type(c_List3**p){return "List";}
class c_Node5 : public Object{
	public:
	c_Node5* m__succ;
	c_Node5* m__pred;
	c_Event* m__data;
	c_Node5();
	c_Node5* m_new(c_Node5*,c_Node5*,c_Event*);
	c_Node5* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node5**p){return "Node";}
class c_HeadNode3 : public c_Node5{
	public:
	c_HeadNode3();
	c_HeadNode3* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode3**p){return "HeadNode";}
int bb_input_MouseDown(int);
extern int bb_event_MOUSE_STATE_NONE;
extern int bb_event_MOUSE_STATE_RIGHT;
extern int bb_event_MOUSE_STATE_BOTH_LEFT;
extern int bb_event_MOUSE_STATE_BOTH_RIGHT;
extern int bb_event_MOUSE_STATE_LEFT;
class c_BackwardsList : public Object{
	public:
	c_List2* m__list;
	c_BackwardsList();
	c_BackwardsList* m_new(c_List2*);
	c_BackwardsList* m_new2();
	c_BackwardsEnumerator* p_ObjectEnumerator();
	void mark();
	String debug();
};
String dbg_type(c_BackwardsList**p){return "BackwardsList";}
class c_BackwardsEnumerator : public Object{
	public:
	c_List2* m__list;
	c_Node4* m__curr;
	c_BackwardsEnumerator();
	c_BackwardsEnumerator* m_new(c_List2*);
	c_BackwardsEnumerator* m_new2();
	bool p_HasNext();
	c_Gadget* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_BackwardsEnumerator**p){return "BackwardsEnumerator";}
int bb_gui_RectangleContainsPoint(int,int,int,int,int,int);
class c_Project : public c_ContainerGadget{
	public:
	c_Patch* m_patch;
	c_Box* m_boxSelected;
	c_View* m_viewPanel;
	c_Panel* m_panel;
	c_Tray* m_tray;
	String m_path;
	c_Project();
	void p_Update();
	c_Project* m_new(String);
	c_Project* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Project**p){return "Project";}
class c_Patch : public c_ViewGadget{
	public:
	c_List4* m_sparks;
	c_List5* m_wires;
	c_List6* m_boxes;
	c_Box* m_boxOver;
	int m_inOver;
	int m_outOver;
	c_Patch();
	void p_UpdateHover(c_Event*,int);
	c_Gadget* p_HandleEvent(c_Event*);
	void p_OnRender();
	void p_OnRenderInterior();
	c_Patch* m_new(int,int,int,int);
	c_Patch* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Patch**p){return "Patch";}
class c_Spark : public Object{
	public:
	int m_arrived;
	int m_n;
	c_Wire* m_wire;
	c_Spark();
	c_Spark* m_new(c_Wire*);
	c_Spark* m_new2();
	void p_Update();
	void p_Render();
	void mark();
	String debug();
};
String dbg_type(c_Spark**p){return "Spark";}
class c_List4 : public Object{
	public:
	c_Node6* m__head;
	c_List4();
	c_List4* m_new();
	c_Node6* p_AddLast4(c_Spark*);
	c_List4* m_new2(Array<c_Spark* >);
	c_Enumerator3* p_ObjectEnumerator();
	bool p_Equals3(c_Spark*,c_Spark*);
	int p_RemoveEach2(c_Spark*);
	void p_Remove3(c_Spark*);
	int p_Clear();
	void mark();
	String debug();
};
String dbg_type(c_List4**p){return "List";}
class c_Node6 : public Object{
	public:
	c_Node6* m__succ;
	c_Node6* m__pred;
	c_Spark* m__data;
	c_Node6();
	c_Node6* m_new(c_Node6*,c_Node6*,c_Spark*);
	c_Node6* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node6**p){return "Node";}
class c_HeadNode4 : public c_Node6{
	public:
	c_HeadNode4();
	c_HeadNode4* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode4**p){return "HeadNode";}
class c_Enumerator3 : public Object{
	public:
	c_List4* m__list;
	c_Node6* m__curr;
	c_Enumerator3();
	c_Enumerator3* m_new(c_List4*);
	c_Enumerator3* m_new2();
	bool p_HasNext();
	c_Spark* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator3**p){return "Enumerator";}
class c_Wire : public Object{
	public:
	c_Box* m_b;
	int m_bId;
	c_Box* m_a;
	c_Wire();
	c_Wire* m_new(c_Box*,c_Box*,int);
	c_Wire* m_new2();
	void p_Render();
	static void m_DrawFrom(c_Box*,int,int);
	static void m_DrawFromTo(c_Box*,c_Box*,int);
	void mark();
	String debug();
};
String dbg_type(c_Wire**p){return "Wire";}
class c_Box : public Object{
	public:
	int m_ins;
	bool m_done;
	String m_kind;
	Array<Array<int > > m_state;
	c_StringMap* m_settings;
	int m_x;
	int m_y;
	int m_w;
	int m_h;
	int m_gap;
	int m_outs;
	int m_id;
	c_Box();
	void p_Execute();
	bool p_isClickable();
	virtual void p_Render();
	static int m_idNext;
	c_Box* m_new(int,int,c_Template*);
	c_Box* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Box**p){return "Box";}
class c_List5 : public Object{
	public:
	c_Node7* m__head;
	c_List5();
	c_List5* m_new();
	c_Node7* p_AddLast5(c_Wire*);
	c_List5* m_new2(Array<c_Wire* >);
	c_Enumerator4* p_ObjectEnumerator();
	bool p_Equals4(c_Wire*,c_Wire*);
	int p_RemoveEach3(c_Wire*);
	void p_Remove4(c_Wire*);
	void mark();
	String debug();
};
String dbg_type(c_List5**p){return "List";}
class c_Node7 : public Object{
	public:
	c_Node7* m__succ;
	c_Node7* m__pred;
	c_Wire* m__data;
	c_Node7();
	c_Node7* m_new(c_Node7*,c_Node7*,c_Wire*);
	c_Node7* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node7**p){return "Node";}
class c_HeadNode5 : public c_Node7{
	public:
	c_HeadNode5();
	c_HeadNode5* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode5**p){return "HeadNode";}
class c_Enumerator4 : public Object{
	public:
	c_List5* m__list;
	c_Node7* m__curr;
	c_Enumerator4();
	c_Enumerator4* m_new(c_List5*);
	c_Enumerator4* m_new2();
	bool p_HasNext();
	c_Wire* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator4**p){return "Enumerator";}
class c_List6 : public Object{
	public:
	c_Node8* m__head;
	c_List6();
	c_List6* m_new();
	c_Node8* p_AddLast6(c_Box*);
	c_List6* m_new2(Array<c_Box* >);
	c_Enumerator5* p_ObjectEnumerator();
	bool p_Equals5(c_Box*,c_Box*);
	int p_RemoveEach4(c_Box*);
	void p_Remove5(c_Box*);
	int p_Count();
	bool p_Contains2(c_Box*);
	void mark();
	String debug();
};
String dbg_type(c_List6**p){return "List";}
class c_Node8 : public Object{
	public:
	c_Node8* m__succ;
	c_Node8* m__pred;
	c_Box* m__data;
	c_Node8();
	c_Node8* m_new(c_Node8*,c_Node8*,c_Box*);
	c_Node8* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node8**p){return "Node";}
class c_HeadNode6 : public c_Node8{
	public:
	c_HeadNode6();
	c_HeadNode6* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode6**p){return "HeadNode";}
class c_Enumerator5 : public Object{
	public:
	c_List6* m__list;
	c_Node8* m__curr;
	c_Enumerator5();
	c_Enumerator5* m_new(c_List6*);
	c_Enumerator5* m_new2();
	bool p_HasNext();
	c_Box* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator5**p){return "Enumerator";}
void bb_functions_Fill(c_Box*,int);
void bb_functions_Clear();
extern int bb_random_Seed;
Float bb_random_Rnd();
Float bb_random_Rnd2(Float,Float);
Float bb_random_Rnd3(Float);
void bb_functions_Noise(c_Box*);
class c_Rules : public Object{
	public:
	Array<int > m_value;
	int m_id;
	c_Rules();
	c_Rules* m_new(int,Array<int >);
	c_Rules* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Rules**p){return "Rules";}
class c_Map3 : public Object{
	public:
	c_Node9* m_root;
	c_Map3();
	c_Map3* m_new();
	virtual int p_Compare(int,int)=0;
	c_Node9* p_FindNode(int);
	c_Rules* p_Get2(int);
	int p_RotateLeft3(c_Node9*);
	int p_RotateRight3(c_Node9*);
	int p_InsertFixup3(c_Node9*);
	bool p_Set3(int,c_Rules*);
	bool p_Insert3(int,c_Rules*);
	void mark();
	String debug();
};
String dbg_type(c_Map3**p){return "Map";}
class c_IntMap2 : public c_Map3{
	public:
	c_IntMap2();
	c_IntMap2* m_new();
	int p_Compare(int,int);
	void mark();
	String debug();
};
String dbg_type(c_IntMap2**p){return "IntMap";}
extern c_IntMap2* bb_gadgets_ruleTables;
class c_Node9 : public Object{
	public:
	int m_key;
	c_Node9* m_right;
	c_Node9* m_left;
	c_Rules* m_value;
	int m_color;
	c_Node9* m_parent;
	c_Node9();
	c_Node9* m_new(int,c_Rules*,int,c_Node9*);
	c_Node9* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node9**p){return "Node";}
Array<Array<int > > bb_box_Initialize2dArray(int,int);
void bb_functions_Automata9Sum8(c_Box*,c_Box*,Array<int >);
void bb_functions_Smooth(c_Box*,c_Box*);
void bb_functions_Automata4(c_Box*,c_Box*,Array<int >);
void bb_functions_Expand(c_Box*,c_Box*);
void bb_functions_Contract(c_Box*,c_Box*);
void bb_functions_Darken(c_Box*,c_Box*,c_Box*);
void bb_functions_Lighten(c_Box*,c_Box*,c_Box*);
void bb_functions_Invert(c_Box*,c_Box*);
void bb_functions_Copy(c_Box*,c_Box*);
void bb_functions_ExecuteBox(c_Box*,Array<c_Box* >);
void bb_main_MakeSparks(c_Box*);
int bb_input_KeyHit(int);
class c_View : public c_Gadget{
	public:
	c_Box* m_box;
	c_View();
	c_Gadget* p_HandleEvent(c_Event*);
	void p_OnRender();
	c_View* m_new(int,int,int,int);
	c_View* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_View**p){return "View";}
int bb_graphics_DebugRenderDevice();
int bb_graphics_Cls(Float,Float,Float);
int bb_graphics_PushMatrix();
int bb_graphics_Transform(Float,Float,Float,Float,Float,Float);
int bb_graphics_Transform2(Array<Float >);
int bb_graphics_Translate(Float,Float);
int bb_graphics_PopMatrix();
extern bool bb_gui__scissorEnabled;
class c_ScissorBox : public Object{
	public:
	Array<Float > m_value;
	c_ScissorBox();
	c_ScissorBox* m_new(Array<Float >);
	c_ScissorBox* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_ScissorBox**p){return "ScissorBox";}
class c_List7 : public Object{
	public:
	c_Node10* m__head;
	c_List7();
	c_List7* m_new();
	c_Node10* p_AddLast7(c_ScissorBox*);
	c_List7* m_new2(Array<c_ScissorBox* >);
	bool p_IsEmpty();
	c_ScissorBox* p_RemoveLast();
	bool p_Equals6(c_ScissorBox*,c_ScissorBox*);
	c_Node10* p_FindLast3(c_ScissorBox*,c_Node10*);
	c_Node10* p_FindLast4(c_ScissorBox*);
	void p_RemoveLast3(c_ScissorBox*);
	void mark();
	String debug();
};
String dbg_type(c_List7**p){return "List";}
class c_Node10 : public Object{
	public:
	c_Node10* m__succ;
	c_Node10* m__pred;
	c_ScissorBox* m__data;
	c_Node10();
	c_Node10* m_new(c_Node10*,c_Node10*,c_ScissorBox*);
	c_Node10* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node10**p){return "Node";}
class c_HeadNode7 : public c_Node10{
	public:
	c_HeadNode7();
	c_HeadNode7* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode7**p){return "HeadNode";}
extern c_List7* bb_gui__scissors;
void bb_gui_PopScissor();
void bb_gui_EnableScissor();
Array<Float > bb_graphics_GetScissor();
int bb_graphics_GetScissor2(Array<Float >);
void bb_gui_PushScissor();
Array<Float > bb_graphics_GetMatrix();
int bb_graphics_GetMatrix2(Array<Float >);
int bb_math_Max(int,int);
Float bb_math_Max2(Float,Float);
int bb_math_Min(int,int);
Float bb_math_Min2(Float,Float);
Array<int > bb_gui_RectangleUnion(int,int,int,int,int,int,int,int);
class c_TabGadget : public c_ContainerGadget{
	public:
	bool m_locked;
	bool m_chosen;
	c_Project* m_project;
	c_BrowserButton* m_closeButton;
	c_BrowserButton* m_saveButton;
	String m_name;
	c_TabGadget();
	c_Gadget* p_HandleEvent(c_Event*);
	void p_HandleGadgetEvent(c_GadgetEvent*);
	void p_OnRender();
	c_TabGadget* m_new(String);
	c_TabGadget* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_TabGadget**p){return "TabGadget";}
class c_List8 : public Object{
	public:
	c_Node11* m__head;
	c_List8();
	c_List8* m_new();
	c_Node11* p_AddLast8(c_TabGadget*);
	c_List8* m_new2(Array<c_TabGadget* >);
	bool p_IsEmpty();
	c_Enumerator6* p_ObjectEnumerator();
	bool p_Equals7(c_TabGadget*,c_TabGadget*);
	int p_RemoveEach5(c_TabGadget*);
	c_Node11* p_AddFirst2(c_TabGadget*);
	c_BackwardsList2* p_Backwards();
	c_TabGadget* p_First();
	void mark();
	String debug();
};
String dbg_type(c_List8**p){return "List";}
class c_Node11 : public Object{
	public:
	c_Node11* m__succ;
	c_Node11* m__pred;
	c_TabGadget* m__data;
	c_Node11();
	c_Node11* m_new(c_Node11*,c_Node11*,c_TabGadget*);
	c_Node11* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node11**p){return "Node";}
class c_HeadNode8 : public c_Node11{
	public:
	c_HeadNode8();
	c_HeadNode8* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode8**p){return "HeadNode";}
class c_Enumerator6 : public Object{
	public:
	c_List8* m__list;
	c_Node11* m__curr;
	c_Enumerator6();
	c_Enumerator6* m_new(c_List8*);
	c_Enumerator6* m_new2();
	bool p_HasNext();
	c_TabGadget* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator6**p){return "Enumerator";}
class c_BackwardsList2 : public Object{
	public:
	c_List8* m__list;
	c_BackwardsList2();
	c_BackwardsList2* m_new(c_List8*);
	c_BackwardsList2* m_new2();
	c_BackwardsEnumerator2* p_ObjectEnumerator();
	void mark();
	String debug();
};
String dbg_type(c_BackwardsList2**p){return "BackwardsList";}
class c_BackwardsEnumerator2 : public Object{
	public:
	c_List8* m__list;
	c_Node11* m__curr;
	c_BackwardsEnumerator2();
	c_BackwardsEnumerator2* m_new(c_List8*);
	c_BackwardsEnumerator2* m_new2();
	bool p_HasNext();
	c_TabGadget* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_BackwardsEnumerator2**p){return "BackwardsEnumerator";}
int bb_graphics_DrawLine(Float,Float,Float,Float);
class c_GadgetEvent : public Object{
	public:
	c_Gadget* m_source;
	c_GadgetEvent();
	c_GadgetEvent* m_new(c_Gadget*);
	c_GadgetEvent* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_GadgetEvent**p){return "GadgetEvent";}
int bb_graphics_DrawRect(Float,Float,Float,Float);
int bb_graphics_DrawImage(c_Image*,Float,Float,int);
int bb_graphics_Rotate(Float);
int bb_graphics_Scale(Float,Float);
int bb_graphics_DrawImage2(c_Image*,Float,Float,Float,Float,Float,int);
extern int bb_main__dragMode;
extern int bb_main_sx;
extern int bb_main_sy;
extern c_Box* bb_main_from;
void bb_main_DeleteWire(c_Wire*);
class c_Panel : public c_ContainerGadget{
	public:
	int m_yMax;
	c_Panel();
	void p_AddChild(c_Gadget*);
	c_Gadget* p_HandleEvent(c_Event*);
	void p__ExecuteBoxSelectedIfSatisfied();
	void p_HandleGadgetEvent(c_GadgetEvent*);
	void p_OnRender();
	void p_OnRenderInterior();
	c_Panel* m_new(int,int,int,int);
	c_Panel* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Panel**p){return "Panel";}
class c_MapValues : public Object{
	public:
	c_Map2* m_map;
	c_MapValues();
	c_MapValues* m_new(c_Map2*);
	c_MapValues* m_new2();
	c_ValueEnumerator* p_ObjectEnumerator();
	void mark();
	String debug();
};
String dbg_type(c_MapValues**p){return "MapValues";}
class c_ValueEnumerator : public Object{
	public:
	c_Node3* m_node;
	c_ValueEnumerator();
	c_ValueEnumerator* m_new(c_Node3*);
	c_ValueEnumerator* m_new2();
	bool p_HasNext();
	c_Setting* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_ValueEnumerator**p){return "ValueEnumerator";}
class c_Slider : public c_Gadget{
	public:
	String m_name;
	int m_index;
	c_Slider();
	c_Slider* m_new(int,int,String,int);
	c_Slider* m_new2();
	c_Gadget* p_HandleEvent(c_Event*);
	void p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_Slider**p){return "Slider";}
class c_NumberBox : public c_ContainerGadget{
	public:
	String m_name;
	int m_value;
	int m_minimum;
	int m_maximum;
	c_ButtonGadget* m_downButton;
	c_ButtonGadget* m_upButton;
	c_NumberBox();
	c_NumberBox* m_new(int,int,String,int,int,int);
	c_NumberBox* m_new2();
	void p_HandleGadgetEvent(c_GadgetEvent*);
	void p_OnRenderInterior();
	void mark();
	String debug();
};
String dbg_type(c_NumberBox**p){return "NumberBox";}
Float bb_graphics_TextWidth(String);
class c_DropList : public c_Gadget{
	public:
	String m_name;
	Array<String > m_values;
	int m_index;
	int m_mode;
	c_DropList();
	c_DropList* m_new(int,int,String,Array<String >,int);
	c_DropList* m_new2();
	c_Gadget* p_HandleEvent(c_Event*);
	void p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_DropList**p){return "DropList";}
class c_CheckBox : public c_Gadget{
	public:
	String m_name;
	int m_on;
	int m_state;
	c_CheckBox();
	c_CheckBox* m_new(int,int,String,int);
	c_CheckBox* m_new2();
	c_Gadget* p_HandleEvent(c_Event*);
	void p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_CheckBox**p){return "CheckBox";}
class c_RuleTable : public c_ContainerGadget{
	public:
	c_Rules* m_rules;
	String m_name;
	Array<c_CheckBox* > m_checkBoxes;
	c_RuleTable();
	c_RuleTable* m_new(int,int,String,int);
	c_RuleTable* m_new2();
	void p_HandleGadgetEvent(c_GadgetEvent*);
	void p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_RuleTable**p){return "RuleTable";}
void bb_patch_SelectBox(c_Box*);
bool bb_main_CycleCheck(c_Box*,c_Box*);
void bb_main_DeleteBox(c_Box*);
class c_ViewBox : public c_Box{
	public:
	c_ViewBox();
	void p_Render();
	c_ViewBox* m_new(int,int);
	c_ViewBox* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_ViewBox**p){return "ViewBox";}
class c_List9 : public Object{
	public:
	c_Node12* m__head;
	c_List9();
	c_List9* m_new();
	c_Node12* p_AddLast9(String);
	c_List9* m_new2(Array<String >);
	bool p_Equals8(String,String);
	bool p_Contains3(String);
	void mark();
	String debug();
};
String dbg_type(c_List9**p){return "List";}
class c_Node12 : public Object{
	public:
	c_Node12* m__succ;
	c_Node12* m__pred;
	String m__data;
	c_Node12();
	c_Node12* m_new(c_Node12*,c_Node12*,String);
	c_Node12* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node12**p){return "Node";}
class c_HeadNode9 : public c_Node12{
	public:
	c_HeadNode9();
	c_HeadNode9* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode9**p){return "HeadNode";}
extern c_List9* bb_functions_implementedTemplates;
int bb_graphics_DrawText(String,Float,Float,Float,Float);
int bb_graphics_DrawOval(Float,Float,Float,Float);
String bb_os_StripExt(String);
String bb_os_StripDir(String);
String bb_os_StripAll(String);
class c_Tray : public c_ViewGadget{
	public:
	c_List6* m_boxes;
	int m_xMax;
	c_Box* m_boxOver;
	c_Box* m_boxSelected;
	c_Tray();
	c_Tray* m_new(int,int,int,int);
	c_Tray* m_new2();
	void p_UpdateHover2(c_Event*);
	c_Gadget* p_HandleEvent(c_Event*);
	void p_OnRender();
	void p_OnRenderInterior();
	void mark();
	String debug();
};
String dbg_type(c_Tray**p){return "Tray";}
void bb_gui_DisableScissor();
c_App::c_App(){
}
c_App* c_App::m_new(){
	DBG_ENTER("App.new")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<152>");
	if((bb_app__app)!=0){
		DBG_BLOCK();
		bbError(String(L"App has already been created",28));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<153>");
	gc_assign(bb_app__app,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<154>");
	gc_assign(bb_app__delegate,(new c_GameDelegate)->m_new());
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<155>");
	bb_app__game->SetDelegate(bb_app__delegate);
	return this;
}
int c_App::p_OnResize(){
	DBG_ENTER("App.OnResize")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnCreate(){
	DBG_ENTER("App.OnCreate")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnSuspend(){
	DBG_ENTER("App.OnSuspend")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnResume(){
	DBG_ENTER("App.OnResume")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnUpdate(){
	DBG_ENTER("App.OnUpdate")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnLoading(){
	DBG_ENTER("App.OnLoading")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnRender(){
	DBG_ENTER("App.OnRender")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnClose(){
	DBG_ENTER("App.OnClose")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<177>");
	bb_app_EndApp();
	return 0;
}
int c_App::p_OnBack(){
	DBG_ENTER("App.OnBack")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<181>");
	p_OnClose();
	return 0;
}
void c_App::mark(){
	Object::mark();
}
String c_App::debug(){
	String t="(App)\n";
	return t;
}
c_MyApp::c_MyApp(){
	m_window=0;
	m_browser=0;
	m__project=0;
}
c_MyApp* c_MyApp::m_new(){
	DBG_ENTER("MyApp.new")
	c_MyApp *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<88>");
	c_App::m_new();
	return this;
}
int c_MyApp::p_OnCreate(){
	DBG_ENTER("MyApp.OnCreate")
	c_MyApp *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<106>");
	bb_app_SetUpdateRate(30);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<108>");
	gc_assign(bb_main_imgX,bb_graphics_LoadImage(String(L"x.png",5),1,1));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<109>");
	bb_main_imgX->p_SetHandle(bb_main_imgX->p_HandleX()-FLOAT(0.5),bb_main_imgX->p_HandleY()-FLOAT(0.5));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<110>");
	gc_assign(bb_main_imgO,bb_graphics_LoadImage(String(L"o.png",5),1,1));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<111>");
	bb_main_imgO->p_SetHandle(bb_main_imgO->p_HandleX()-FLOAT(0.5),bb_main_imgO->p_HandleY()-FLOAT(0.5));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<112>");
	int t_[]={9,8,1,1};
	int t_2[]={-1};
	gc_assign(bb_main_imgTab,(new c_NinePatchImage)->m_new(bb_graphics_LoadImage(String(L"tab.png",7),1,c_Image::m_DefaultFlags),Array<int >(t_,4),Array<int >(t_2,1)));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<113>");
	gc_assign(bb_main_imgClose,bb_graphics_LoadImage(String(L"close.png",9),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<114>");
	gc_assign(bb_main_imgOpen,bb_graphics_LoadImage(String(L"open.png",8),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<115>");
	gc_assign(bb_main_imgSave,bb_graphics_LoadImage(String(L"save.png",8),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<116>");
	gc_assign(bb_main_imgNew,bb_graphics_LoadImage(String(L"new.png",7),1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<117>");
	bb_graphics_SetFont(bb_graphics_LoadImage(String(L"font.png",8),96,2),32);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<119>");
	bb_template_MakeTemplates();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<121>");
	gc_assign(m_window,(new c_WindowGadget)->m_new(0,0,640,480));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<122>");
	gc_assign(c_Event::m_globalWindow,m_window);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<123>");
	gc_assign(m_browser,(new c_Browser)->m_new(0,1,640,18));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<124>");
	m_window->p_AddChild(m_browser);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<126>");
	return 0;
}
c_Project* c_MyApp::p_project(){
	DBG_ENTER("MyApp.project")
	c_MyApp *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<92>");
	return m__project;
}
void c_MyApp::p_project2(c_Project* t_value){
	DBG_ENTER("MyApp.project")
	c_MyApp *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<95>");
	if(m__project!=0){
		DBG_BLOCK();
		m__project->p_Disable();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<96>");
	gc_assign(m__project,t_value);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<97>");
	if(m__project!=0){
		DBG_BLOCK();
		m__project->p_Enable();
	}
}
c_Patch* c_MyApp::p_patch(){
	DBG_ENTER("MyApp.patch")
	c_MyApp *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<100>");
	c_Patch* t_=p_project()->m_patch;
	return t_;
}
int c_MyApp::p_OnUpdate(){
	DBG_ENTER("MyApp.OnUpdate")
	c_MyApp *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<130>");
	m_window->p_Update();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<132>");
	while(m_window->m__events->p_Count()>0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<133>");
		m_window->p_HandleEvent(m_window->m__events->p_RemoveFirst());
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<136>");
	if(m__project!=0){
		DBG_BLOCK();
		m__project->p_Update();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<138>");
	return 0;
}
int c_MyApp::p_OnRender(){
	DBG_ENTER("MyApp.OnRender")
	c_MyApp *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<142>");
	bb_graphics_Cls(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<143>");
	bb_graphics_SetMatrix(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<144>");
	m_window->p_Render();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<145>");
	return 0;
}
c_Panel* c_MyApp::p_panel(){
	DBG_ENTER("MyApp.panel")
	c_MyApp *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<101>");
	c_Panel* t_=p_project()->m_panel;
	return t_;
}
void c_MyApp::mark(){
	c_App::mark();
	gc_mark_q(m_window);
	gc_mark_q(m_browser);
	gc_mark_q(m__project);
}
String c_MyApp::debug(){
	String t="(MyApp)\n";
	t=c_App::debug()+t;
	t+=dbg_decl("window",&m_window);
	t+=dbg_decl("browser",&m_browser);
	t+=dbg_decl("_project",&m__project);
	return t;
}
c_App* bb_app__app;
c_GameDelegate::c_GameDelegate(){
	m__graphics=0;
	m__audio=0;
	m__input=0;
}
c_GameDelegate* c_GameDelegate::m_new(){
	DBG_ENTER("GameDelegate.new")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<65>");
	return this;
}
void c_GameDelegate::StartGame(){
	DBG_ENTER("GameDelegate.StartGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<75>");
	gc_assign(m__graphics,(new gxtkGraphics));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<76>");
	bb_graphics_SetGraphicsDevice(m__graphics);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<77>");
	bb_graphics_SetFont(0,32);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<79>");
	gc_assign(m__audio,(new gxtkAudio));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<80>");
	bb_audio_SetAudioDevice(m__audio);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<82>");
	gc_assign(m__input,(new c_InputDevice)->m_new());
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<83>");
	bb_input_SetInputDevice(m__input);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<85>");
	bb_app_ValidateDeviceWindow(false);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<87>");
	bb_app_EnumDisplayModes();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<89>");
	bb_app__app->p_OnCreate();
}
void c_GameDelegate::SuspendGame(){
	DBG_ENTER("GameDelegate.SuspendGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<93>");
	bb_app__app->p_OnSuspend();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<94>");
	m__audio->Suspend();
}
void c_GameDelegate::ResumeGame(){
	DBG_ENTER("GameDelegate.ResumeGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<98>");
	m__audio->Resume();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<99>");
	bb_app__app->p_OnResume();
}
void c_GameDelegate::UpdateGame(){
	DBG_ENTER("GameDelegate.UpdateGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<103>");
	bb_app_ValidateDeviceWindow(true);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<104>");
	m__input->p_BeginUpdate();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<105>");
	bb_app__app->p_OnUpdate();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<106>");
	m__input->p_EndUpdate();
}
void c_GameDelegate::RenderGame(){
	DBG_ENTER("GameDelegate.RenderGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<110>");
	bb_app_ValidateDeviceWindow(true);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<111>");
	int t_mode=m__graphics->BeginRender();
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<112>");
	if((t_mode)!=0){
		DBG_BLOCK();
		bb_graphics_BeginRender();
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<113>");
	if(t_mode==2){
		DBG_BLOCK();
		bb_app__app->p_OnLoading();
	}else{
		DBG_BLOCK();
		bb_app__app->p_OnRender();
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<114>");
	if((t_mode)!=0){
		DBG_BLOCK();
		bb_graphics_EndRender();
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<115>");
	m__graphics->EndRender();
}
void c_GameDelegate::KeyEvent(int t_event,int t_data){
	DBG_ENTER("GameDelegate.KeyEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<119>");
	m__input->p_KeyEvent(t_event,t_data);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<120>");
	if(t_event!=1){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<121>");
	int t_1=t_data;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<122>");
	if(t_1==432){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<123>");
		bb_app__app->p_OnClose();
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<124>");
		if(t_1==416){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<125>");
			bb_app__app->p_OnBack();
		}
	}
}
void c_GameDelegate::MouseEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("GameDelegate.MouseEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<130>");
	m__input->p_MouseEvent(t_event,t_data,t_x,t_y);
}
void c_GameDelegate::TouchEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("GameDelegate.TouchEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<134>");
	m__input->p_TouchEvent(t_event,t_data,t_x,t_y);
}
void c_GameDelegate::MotionEvent(int t_event,int t_data,Float t_x,Float t_y,Float t_z){
	DBG_ENTER("GameDelegate.MotionEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_z,"z")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<138>");
	m__input->p_MotionEvent(t_event,t_data,t_x,t_y,t_z);
}
void c_GameDelegate::DiscardGraphics(){
	DBG_ENTER("GameDelegate.DiscardGraphics")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<142>");
	m__graphics->DiscardGraphics();
}
void c_GameDelegate::mark(){
	BBGameDelegate::mark();
	gc_mark_q(m__graphics);
	gc_mark_q(m__audio);
	gc_mark_q(m__input);
}
String c_GameDelegate::debug(){
	String t="(GameDelegate)\n";
	t+=dbg_decl("_graphics",&m__graphics);
	t+=dbg_decl("_audio",&m__audio);
	t+=dbg_decl("_input",&m__input);
	return t;
}
c_GameDelegate* bb_app__delegate;
BBGame* bb_app__game;
c_MyApp* bb_main_APP;
int bbMain(){
	DBG_ENTER("Main")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<78>");
	gc_assign(bb_main_APP,(new c_MyApp)->m_new());
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<79>");
	return 0;
}
gxtkGraphics* bb_graphics_device;
int bb_graphics_SetGraphicsDevice(gxtkGraphics* t_dev){
	DBG_ENTER("SetGraphicsDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<63>");
	gc_assign(bb_graphics_device,t_dev);
	return 0;
}
c_Image::c_Image(){
	m_surface=0;
	m_width=0;
	m_height=0;
	m_frames=Array<c_Frame* >();
	m_flags=0;
	m_tx=FLOAT(.0);
	m_ty=FLOAT(.0);
	m_source=0;
}
int c_Image::m_DefaultFlags;
c_Image* c_Image::m_new(){
	DBG_ENTER("Image.new")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<70>");
	return this;
}
int c_Image::p_SetHandle(Float t_tx,Float t_ty){
	DBG_ENTER("Image.SetHandle")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<114>");
	this->m_tx=t_tx;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<115>");
	this->m_ty=t_ty;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<116>");
	this->m_flags=this->m_flags&-2;
	return 0;
}
int c_Image::p_ApplyFlags(int t_iflags){
	DBG_ENTER("Image.ApplyFlags")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<197>");
	m_flags=t_iflags;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<199>");
	if((m_flags&2)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<200>");
		Array<c_Frame* > t_=m_frames;
		int t_2=0;
		while(t_2<t_.Length()){
			DBG_BLOCK();
			c_Frame* t_f=t_.At(t_2);
			t_2=t_2+1;
			DBG_LOCAL(t_f,"f")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<201>");
			t_f->m_x+=1;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<203>");
		m_width-=2;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<206>");
	if((m_flags&4)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<207>");
		Array<c_Frame* > t_3=m_frames;
		int t_4=0;
		while(t_4<t_3.Length()){
			DBG_BLOCK();
			c_Frame* t_f2=t_3.At(t_4);
			t_4=t_4+1;
			DBG_LOCAL(t_f2,"f")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<208>");
			t_f2->m_y+=1;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<210>");
		m_height-=2;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<213>");
	if((m_flags&1)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<214>");
		p_SetHandle(Float(m_width)/FLOAT(2.0),Float(m_height)/FLOAT(2.0));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<217>");
	if(m_frames.Length()==1 && m_frames.At(0)->m_x==0 && m_frames.At(0)->m_y==0 && m_width==m_surface->Width() && m_height==m_surface->Height()){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<218>");
		m_flags|=65536;
	}
	return 0;
}
c_Image* c_Image::p_Init(gxtkSurface* t_surf,int t_nframes,int t_iflags){
	DBG_ENTER("Image.Init")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_surf,"surf")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<143>");
	if((m_surface)!=0){
		DBG_BLOCK();
		bbError(String(L"Image already initialized",25));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<144>");
	gc_assign(m_surface,t_surf);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<146>");
	m_width=m_surface->Width()/t_nframes;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<147>");
	m_height=m_surface->Height();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<149>");
	gc_assign(m_frames,Array<c_Frame* >(t_nframes));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<150>");
	for(int t_i=0;t_i<t_nframes;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<151>");
		gc_assign(m_frames.At(t_i),(new c_Frame)->m_new(t_i*m_width,0));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<154>");
	p_ApplyFlags(t_iflags);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<155>");
	return this;
}
c_Image* c_Image::p_Init2(gxtkSurface* t_surf,int t_x,int t_y,int t_iwidth,int t_iheight,int t_nframes,int t_iflags,c_Image* t_src,int t_srcx,int t_srcy,int t_srcw,int t_srch){
	DBG_ENTER("Image.Init")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_surf,"surf")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_iwidth,"iwidth")
	DBG_LOCAL(t_iheight,"iheight")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_LOCAL(t_src,"src")
	DBG_LOCAL(t_srcx,"srcx")
	DBG_LOCAL(t_srcy,"srcy")
	DBG_LOCAL(t_srcw,"srcw")
	DBG_LOCAL(t_srch,"srch")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<159>");
	if((m_surface)!=0){
		DBG_BLOCK();
		bbError(String(L"Image already initialized",25));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<160>");
	gc_assign(m_surface,t_surf);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<161>");
	gc_assign(m_source,t_src);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<163>");
	m_width=t_iwidth;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<164>");
	m_height=t_iheight;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<166>");
	gc_assign(m_frames,Array<c_Frame* >(t_nframes));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<168>");
	int t_ix=t_x;
	int t_iy=t_y;
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<170>");
	for(int t_i=0;t_i<t_nframes;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<171>");
		if(t_ix+m_width>t_srcw){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<172>");
			t_ix=0;
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<173>");
			t_iy+=m_height;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<175>");
		if(t_ix+m_width>t_srcw || t_iy+m_height>t_srch){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<176>");
			bbError(String(L"Image frame outside surface",27));
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<178>");
		gc_assign(m_frames.At(t_i),(new c_Frame)->m_new(t_ix+t_srcx,t_iy+t_srcy));
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<179>");
		t_ix+=m_width;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<182>");
	p_ApplyFlags(t_iflags);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<183>");
	return this;
}
Float c_Image::p_HandleX(){
	DBG_ENTER("Image.HandleX")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<101>");
	return m_tx;
}
Float c_Image::p_HandleY(){
	DBG_ENTER("Image.HandleY")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<105>");
	return m_ty;
}
int c_Image::p_Width(){
	DBG_ENTER("Image.Width")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<81>");
	return m_width;
}
int c_Image::p_Height(){
	DBG_ENTER("Image.Height")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<85>");
	return m_height;
}
c_Image* c_Image::p_GrabImage(int t_x,int t_y,int t_width,int t_height,int t_nframes,int t_flags){
	DBG_ENTER("Image.GrabImage")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<109>");
	if(m_frames.Length()!=1){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<110>");
	c_Image* t_=((new c_Image)->m_new())->p_Init2(m_surface,t_x,t_y,t_width,t_height,t_nframes,t_flags,this,m_frames.At(0)->m_x,m_frames.At(0)->m_y,this->m_width,this->m_height);
	return t_;
}
int c_Image::p_Frames(){
	DBG_ENTER("Image.Frames")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<93>");
	int t_=m_frames.Length();
	return t_;
}
void c_Image::mark(){
	Object::mark();
	gc_mark_q(m_surface);
	gc_mark_q(m_frames);
	gc_mark_q(m_source);
}
String c_Image::debug(){
	String t="(Image)\n";
	t+=dbg_decl("DefaultFlags",&c_Image::m_DefaultFlags);
	t+=dbg_decl("source",&m_source);
	t+=dbg_decl("surface",&m_surface);
	t+=dbg_decl("width",&m_width);
	t+=dbg_decl("height",&m_height);
	t+=dbg_decl("flags",&m_flags);
	t+=dbg_decl("frames",&m_frames);
	t+=dbg_decl("tx",&m_tx);
	t+=dbg_decl("ty",&m_ty);
	return t;
}
c_GraphicsContext::c_GraphicsContext(){
	m_defaultFont=0;
	m_font=0;
	m_firstChar=0;
	m_matrixSp=0;
	m_ix=FLOAT(1.0);
	m_iy=FLOAT(.0);
	m_jx=FLOAT(.0);
	m_jy=FLOAT(1.0);
	m_tx=FLOAT(.0);
	m_ty=FLOAT(.0);
	m_tformed=0;
	m_matDirty=0;
	m_color_r=FLOAT(.0);
	m_color_g=FLOAT(.0);
	m_color_b=FLOAT(.0);
	m_alpha=FLOAT(.0);
	m_blend=0;
	m_scissor_x=FLOAT(.0);
	m_scissor_y=FLOAT(.0);
	m_scissor_width=FLOAT(.0);
	m_scissor_height=FLOAT(.0);
	m_matrixStack=Array<Float >(192);
}
c_GraphicsContext* c_GraphicsContext::m_new(){
	DBG_ENTER("GraphicsContext.new")
	c_GraphicsContext *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<29>");
	return this;
}
int c_GraphicsContext::p_Validate(){
	DBG_ENTER("GraphicsContext.Validate")
	c_GraphicsContext *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<40>");
	if((m_matDirty)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<41>");
		bb_graphics_renderDevice->SetMatrix(bb_graphics_context->m_ix,bb_graphics_context->m_iy,bb_graphics_context->m_jx,bb_graphics_context->m_jy,bb_graphics_context->m_tx,bb_graphics_context->m_ty);
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<42>");
		m_matDirty=0;
	}
	return 0;
}
void c_GraphicsContext::mark(){
	Object::mark();
	gc_mark_q(m_defaultFont);
	gc_mark_q(m_font);
	gc_mark_q(m_matrixStack);
}
String c_GraphicsContext::debug(){
	String t="(GraphicsContext)\n";
	t+=dbg_decl("color_r",&m_color_r);
	t+=dbg_decl("color_g",&m_color_g);
	t+=dbg_decl("color_b",&m_color_b);
	t+=dbg_decl("alpha",&m_alpha);
	t+=dbg_decl("blend",&m_blend);
	t+=dbg_decl("ix",&m_ix);
	t+=dbg_decl("iy",&m_iy);
	t+=dbg_decl("jx",&m_jx);
	t+=dbg_decl("jy",&m_jy);
	t+=dbg_decl("tx",&m_tx);
	t+=dbg_decl("ty",&m_ty);
	t+=dbg_decl("tformed",&m_tformed);
	t+=dbg_decl("matDirty",&m_matDirty);
	t+=dbg_decl("scissor_x",&m_scissor_x);
	t+=dbg_decl("scissor_y",&m_scissor_y);
	t+=dbg_decl("scissor_width",&m_scissor_width);
	t+=dbg_decl("scissor_height",&m_scissor_height);
	t+=dbg_decl("matrixStack",&m_matrixStack);
	t+=dbg_decl("matrixSp",&m_matrixSp);
	t+=dbg_decl("font",&m_font);
	t+=dbg_decl("firstChar",&m_firstChar);
	t+=dbg_decl("defaultFont",&m_defaultFont);
	return t;
}
c_GraphicsContext* bb_graphics_context;
String bb_data_FixDataPath(String t_path){
	DBG_ENTER("FixDataPath")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/data.monkey<7>");
	int t_i=t_path.Find(String(L":/",2),0);
	DBG_LOCAL(t_i,"i")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/data.monkey<8>");
	if(t_i!=-1 && t_path.Find(String(L"/",1),0)==t_i+1){
		DBG_BLOCK();
		return t_path;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/data.monkey<9>");
	if(t_path.StartsWith(String(L"./",2)) || t_path.StartsWith(String(L"/",1))){
		DBG_BLOCK();
		return t_path;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/data.monkey<10>");
	String t_=String(L"monkey://data/",14)+t_path;
	return t_;
}
c_Frame::c_Frame(){
	m_x=0;
	m_y=0;
}
c_Frame* c_Frame::m_new(int t_x,int t_y){
	DBG_ENTER("Frame.new")
	c_Frame *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<23>");
	this->m_x=t_x;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<24>");
	this->m_y=t_y;
	return this;
}
c_Frame* c_Frame::m_new2(){
	DBG_ENTER("Frame.new")
	c_Frame *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<18>");
	return this;
}
void c_Frame::mark(){
	Object::mark();
}
String c_Frame::debug(){
	String t="(Frame)\n";
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	return t;
}
c_Image* bb_graphics_LoadImage(String t_path,int t_frameCount,int t_flags){
	DBG_ENTER("LoadImage")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<239>");
	gxtkSurface* t_surf=bb_graphics_device->LoadSurface(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<240>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init(t_surf,t_frameCount,t_flags);
		return t_;
	}
	return 0;
}
c_Image* bb_graphics_LoadImage2(String t_path,int t_frameWidth,int t_frameHeight,int t_frameCount,int t_flags){
	DBG_ENTER("LoadImage")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_frameWidth,"frameWidth")
	DBG_LOCAL(t_frameHeight,"frameHeight")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<244>");
	gxtkSurface* t_surf=bb_graphics_device->LoadSurface(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<245>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init2(t_surf,0,0,t_frameWidth,t_frameHeight,t_frameCount,t_flags,0,0,0,t_surf->Width(),t_surf->Height());
		return t_;
	}
	return 0;
}
int bb_graphics_SetFont(c_Image* t_font,int t_firstChar){
	DBG_ENTER("SetFont")
	DBG_LOCAL(t_font,"font")
	DBG_LOCAL(t_firstChar,"firstChar")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<547>");
	if(!((t_font)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<548>");
		if(!((bb_graphics_context->m_defaultFont)!=0)){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<549>");
			gc_assign(bb_graphics_context->m_defaultFont,bb_graphics_LoadImage(String(L"mojo_font.png",13),96,2));
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<551>");
		t_font=bb_graphics_context->m_defaultFont;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<552>");
		t_firstChar=32;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<554>");
	gc_assign(bb_graphics_context->m_font,t_font);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<555>");
	bb_graphics_context->m_firstChar=t_firstChar;
	return 0;
}
gxtkAudio* bb_audio_device;
int bb_audio_SetAudioDevice(gxtkAudio* t_dev){
	DBG_ENTER("SetAudioDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/audio.monkey<22>");
	gc_assign(bb_audio_device,t_dev);
	return 0;
}
c_InputDevice::c_InputDevice(){
	m__joyStates=Array<c_JoyState* >(4);
	m__keyDown=Array<bool >(512);
	m__keyHitPut=0;
	m__keyHitQueue=Array<int >(33);
	m__keyHit=Array<int >(512);
	m__charGet=0;
	m__charPut=0;
	m__charQueue=Array<int >(32);
	m__mouseX=FLOAT(.0);
	m__mouseY=FLOAT(.0);
	m__touchX=Array<Float >(32);
	m__touchY=Array<Float >(32);
	m__accelX=FLOAT(.0);
	m__accelY=FLOAT(.0);
	m__accelZ=FLOAT(.0);
}
c_InputDevice* c_InputDevice::m_new(){
	DBG_ENTER("InputDevice.new")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<26>");
	for(int t_i=0;t_i<4;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<27>");
		gc_assign(m__joyStates.At(t_i),(new c_JoyState)->m_new());
	}
	return this;
}
void c_InputDevice::p_PutKeyHit(int t_key){
	DBG_ENTER("InputDevice.PutKeyHit")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<237>");
	if(m__keyHitPut==m__keyHitQueue.Length()){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<238>");
	m__keyHit.At(t_key)+=1;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<239>");
	m__keyHitQueue.At(m__keyHitPut)=t_key;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<240>");
	m__keyHitPut+=1;
}
void c_InputDevice::p_BeginUpdate(){
	DBG_ENTER("InputDevice.BeginUpdate")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<189>");
	for(int t_i=0;t_i<4;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<190>");
		c_JoyState* t_state=m__joyStates.At(t_i);
		DBG_LOCAL(t_state,"state")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<191>");
		if(!BBGame::Game()->PollJoystick(t_i,t_state->m_joyx,t_state->m_joyy,t_state->m_joyz,t_state->m_buttons)){
			DBG_BLOCK();
			break;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<192>");
		for(int t_j=0;t_j<32;t_j=t_j+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<193>");
			int t_key=256+t_i*32+t_j;
			DBG_LOCAL(t_key,"key")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<194>");
			if(t_state->m_buttons.At(t_j)){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<195>");
				if(!m__keyDown.At(t_key)){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<196>");
					m__keyDown.At(t_key)=true;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<197>");
					p_PutKeyHit(t_key);
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<200>");
				m__keyDown.At(t_key)=false;
			}
		}
	}
}
void c_InputDevice::p_EndUpdate(){
	DBG_ENTER("InputDevice.EndUpdate")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<207>");
	for(int t_i=0;t_i<m__keyHitPut;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<208>");
		m__keyHit.At(m__keyHitQueue.At(t_i))=0;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<210>");
	m__keyHitPut=0;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<211>");
	m__charGet=0;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<212>");
	m__charPut=0;
}
void c_InputDevice::p_KeyEvent(int t_event,int t_data){
	DBG_ENTER("InputDevice.KeyEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<111>");
	int t_1=t_event;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<112>");
	if(t_1==1){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<113>");
		if(!m__keyDown.At(t_data)){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<114>");
			m__keyDown.At(t_data)=true;
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<115>");
			p_PutKeyHit(t_data);
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<116>");
			if(t_data==1){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<117>");
				m__keyDown.At(384)=true;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<118>");
				p_PutKeyHit(384);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<119>");
				if(t_data==384){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<120>");
					m__keyDown.At(1)=true;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<121>");
					p_PutKeyHit(1);
				}
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<124>");
		if(t_1==2){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<125>");
			if(m__keyDown.At(t_data)){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<126>");
				m__keyDown.At(t_data)=false;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<127>");
				if(t_data==1){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<128>");
					m__keyDown.At(384)=false;
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<129>");
					if(t_data==384){
						DBG_BLOCK();
						DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<130>");
						m__keyDown.At(1)=false;
					}
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<133>");
			if(t_1==3){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<134>");
				if(m__charPut<m__charQueue.Length()){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<135>");
					m__charQueue.At(m__charPut)=t_data;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<136>");
					m__charPut+=1;
				}
			}
		}
	}
}
void c_InputDevice::p_MouseEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("InputDevice.MouseEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<142>");
	int t_2=t_event;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<143>");
	if(t_2==4){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<144>");
		p_KeyEvent(1,1+t_data);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<145>");
		if(t_2==5){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<146>");
			p_KeyEvent(2,1+t_data);
			return;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<148>");
			if(t_2==6){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				return;
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<152>");
	m__mouseX=t_x;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<153>");
	m__mouseY=t_y;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<154>");
	m__touchX.At(0)=t_x;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<155>");
	m__touchY.At(0)=t_y;
}
void c_InputDevice::p_TouchEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("InputDevice.TouchEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<159>");
	int t_3=t_event;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<160>");
	if(t_3==7){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<161>");
		p_KeyEvent(1,384+t_data);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<162>");
		if(t_3==8){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<163>");
			p_KeyEvent(2,384+t_data);
			return;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<165>");
			if(t_3==9){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				return;
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<169>");
	m__touchX.At(t_data)=t_x;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<170>");
	m__touchY.At(t_data)=t_y;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<171>");
	if(t_data==0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<172>");
		m__mouseX=t_x;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<173>");
		m__mouseY=t_y;
	}
}
void c_InputDevice::p_MotionEvent(int t_event,int t_data,Float t_x,Float t_y,Float t_z){
	DBG_ENTER("InputDevice.MotionEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_z,"z")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<178>");
	int t_4=t_event;
	DBG_LOCAL(t_4,"4")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<179>");
	if(t_4==10){
		DBG_BLOCK();
	}else{
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<183>");
	m__accelX=t_x;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<184>");
	m__accelY=t_y;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<185>");
	m__accelZ=t_z;
}
Float c_InputDevice::p_MouseX(){
	DBG_ENTER("InputDevice.MouseX")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<69>");
	return m__mouseX;
}
Float c_InputDevice::p_MouseY(){
	DBG_ENTER("InputDevice.MouseY")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<73>");
	return m__mouseY;
}
bool c_InputDevice::p_KeyDown(int t_key){
	DBG_ENTER("InputDevice.KeyDown")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<47>");
	if(t_key>0 && t_key<512){
		DBG_BLOCK();
		return m__keyDown.At(t_key);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<48>");
	return false;
}
int c_InputDevice::p_KeyHit(int t_key){
	DBG_ENTER("InputDevice.KeyHit")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<52>");
	if(t_key>0 && t_key<512){
		DBG_BLOCK();
		return m__keyHit.At(t_key);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<53>");
	return 0;
}
void c_InputDevice::mark(){
	Object::mark();
	gc_mark_q(m__joyStates);
	gc_mark_q(m__keyDown);
	gc_mark_q(m__keyHitQueue);
	gc_mark_q(m__keyHit);
	gc_mark_q(m__charQueue);
	gc_mark_q(m__touchX);
	gc_mark_q(m__touchY);
}
String c_InputDevice::debug(){
	String t="(InputDevice)\n";
	t+=dbg_decl("_keyDown",&m__keyDown);
	t+=dbg_decl("_keyHit",&m__keyHit);
	t+=dbg_decl("_keyHitQueue",&m__keyHitQueue);
	t+=dbg_decl("_keyHitPut",&m__keyHitPut);
	t+=dbg_decl("_charQueue",&m__charQueue);
	t+=dbg_decl("_charPut",&m__charPut);
	t+=dbg_decl("_charGet",&m__charGet);
	t+=dbg_decl("_mouseX",&m__mouseX);
	t+=dbg_decl("_mouseY",&m__mouseY);
	t+=dbg_decl("_touchX",&m__touchX);
	t+=dbg_decl("_touchY",&m__touchY);
	t+=dbg_decl("_accelX",&m__accelX);
	t+=dbg_decl("_accelY",&m__accelY);
	t+=dbg_decl("_accelZ",&m__accelZ);
	t+=dbg_decl("_joyStates",&m__joyStates);
	return t;
}
c_JoyState::c_JoyState(){
	m_joyx=Array<Float >(2);
	m_joyy=Array<Float >(2);
	m_joyz=Array<Float >(2);
	m_buttons=Array<bool >(32);
}
c_JoyState* c_JoyState::m_new(){
	DBG_ENTER("JoyState.new")
	c_JoyState *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/inputdevice.monkey<14>");
	return this;
}
void c_JoyState::mark(){
	Object::mark();
	gc_mark_q(m_joyx);
	gc_mark_q(m_joyy);
	gc_mark_q(m_joyz);
	gc_mark_q(m_buttons);
}
String c_JoyState::debug(){
	String t="(JoyState)\n";
	t+=dbg_decl("joyx",&m_joyx);
	t+=dbg_decl("joyy",&m_joyy);
	t+=dbg_decl("joyz",&m_joyz);
	t+=dbg_decl("buttons",&m_buttons);
	return t;
}
c_InputDevice* bb_input_device;
int bb_input_SetInputDevice(c_InputDevice* t_dev){
	DBG_ENTER("SetInputDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/input.monkey<22>");
	gc_assign(bb_input_device,t_dev);
	return 0;
}
int bb_app__devWidth;
int bb_app__devHeight;
void bb_app_ValidateDeviceWindow(bool t_notifyApp){
	DBG_ENTER("ValidateDeviceWindow")
	DBG_LOCAL(t_notifyApp,"notifyApp")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<57>");
	int t_w=bb_app__game->GetDeviceWidth();
	DBG_LOCAL(t_w,"w")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<58>");
	int t_h=bb_app__game->GetDeviceHeight();
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<59>");
	if(t_w==bb_app__devWidth && t_h==bb_app__devHeight){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<60>");
	bb_app__devWidth=t_w;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<61>");
	bb_app__devHeight=t_h;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<62>");
	if(t_notifyApp){
		DBG_BLOCK();
		bb_app__app->p_OnResize();
	}
}
c_DisplayMode::c_DisplayMode(){
	m__width=0;
	m__height=0;
}
c_DisplayMode* c_DisplayMode::m_new(int t_width,int t_height){
	DBG_ENTER("DisplayMode.new")
	c_DisplayMode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<192>");
	m__width=t_width;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<193>");
	m__height=t_height;
	return this;
}
c_DisplayMode* c_DisplayMode::m_new2(){
	DBG_ENTER("DisplayMode.new")
	c_DisplayMode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<189>");
	return this;
}
void c_DisplayMode::mark(){
	Object::mark();
}
String c_DisplayMode::debug(){
	String t="(DisplayMode)\n";
	t+=dbg_decl("_width",&m__width);
	t+=dbg_decl("_height",&m__height);
	return t;
}
c_Map::c_Map(){
	m_root=0;
}
c_Map* c_Map::m_new(){
	DBG_ENTER("Map.new")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<7>");
	return this;
}
c_Node* c_Map::p_FindNode(int t_key){
	DBG_ENTER("Map.FindNode")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<157>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<159>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<160>");
		int t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_LOCAL(t_cmp,"cmp")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<161>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<162>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<163>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<164>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<166>");
				return t_node;
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<169>");
	return t_node;
}
bool c_Map::p_Contains(int t_key){
	DBG_ENTER("Map.Contains")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<25>");
	bool t_=p_FindNode(t_key)!=0;
	return t_;
}
int c_Map::p_RotateLeft(c_Node* t_node){
	DBG_ENTER("Map.RotateLeft")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<251>");
	c_Node* t_child=t_node->m_right;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<252>");
	gc_assign(t_node->m_right,t_child->m_left);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<253>");
	if((t_child->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<254>");
		gc_assign(t_child->m_left->m_parent,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<256>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<257>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<258>");
		if(t_node==t_node->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<259>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<261>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<264>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<266>");
	gc_assign(t_child->m_left,t_node);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<267>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_RotateRight(c_Node* t_node){
	DBG_ENTER("Map.RotateRight")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<271>");
	c_Node* t_child=t_node->m_left;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<272>");
	gc_assign(t_node->m_left,t_child->m_right);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<273>");
	if((t_child->m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<274>");
		gc_assign(t_child->m_right->m_parent,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<276>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<277>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<278>");
		if(t_node==t_node->m_parent->m_right){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<279>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<281>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<284>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<286>");
	gc_assign(t_child->m_right,t_node);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<287>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_InsertFixup(c_Node* t_node){
	DBG_ENTER("Map.InsertFixup")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<212>");
	while(((t_node->m_parent)!=0) && t_node->m_parent->m_color==-1 && ((t_node->m_parent->m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<213>");
		if(t_node->m_parent==t_node->m_parent->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<214>");
			c_Node* t_uncle=t_node->m_parent->m_parent->m_right;
			DBG_LOCAL(t_uncle,"uncle")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<215>");
			if(((t_uncle)!=0) && t_uncle->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<216>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<217>");
				t_uncle->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<218>");
				t_uncle->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<219>");
				t_node=t_uncle->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<221>");
				if(t_node==t_node->m_parent->m_right){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<222>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<223>");
					p_RotateLeft(t_node);
				}
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<225>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<226>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<227>");
				p_RotateRight(t_node->m_parent->m_parent);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<230>");
			c_Node* t_uncle2=t_node->m_parent->m_parent->m_left;
			DBG_LOCAL(t_uncle2,"uncle")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<231>");
			if(((t_uncle2)!=0) && t_uncle2->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<232>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<233>");
				t_uncle2->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<234>");
				t_uncle2->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<235>");
				t_node=t_uncle2->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<237>");
				if(t_node==t_node->m_parent->m_left){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<238>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<239>");
					p_RotateRight(t_node);
				}
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<241>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<242>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<243>");
				p_RotateLeft(t_node->m_parent->m_parent);
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<247>");
	m_root->m_color=1;
	return 0;
}
bool c_Map::p_Set(int t_key,c_DisplayMode* t_value){
	DBG_ENTER("Map.Set")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<29>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<30>");
	c_Node* t_parent=0;
	int t_cmp=0;
	DBG_LOCAL(t_parent,"parent")
	DBG_LOCAL(t_cmp,"cmp")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<32>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<33>");
		t_parent=t_node;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<34>");
		t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<35>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<36>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<37>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<38>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<40>");
				gc_assign(t_node->m_value,t_value);
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<41>");
				return false;
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<45>");
	t_node=(new c_Node)->m_new(t_key,t_value,-1,t_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<47>");
	if((t_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<48>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<49>");
			gc_assign(t_parent->m_right,t_node);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<51>");
			gc_assign(t_parent->m_left,t_node);
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<53>");
		p_InsertFixup(t_node);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<55>");
		gc_assign(m_root,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<57>");
	return true;
}
bool c_Map::p_Insert(int t_key,c_DisplayMode* t_value){
	DBG_ENTER("Map.Insert")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<146>");
	bool t_=p_Set(t_key,t_value);
	return t_;
}
void c_Map::mark(){
	Object::mark();
	gc_mark_q(m_root);
}
String c_Map::debug(){
	String t="(Map)\n";
	t+=dbg_decl("root",&m_root);
	return t;
}
c_IntMap::c_IntMap(){
}
c_IntMap* c_IntMap::m_new(){
	DBG_ENTER("IntMap.new")
	c_IntMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<534>");
	c_Map::m_new();
	return this;
}
int c_IntMap::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("IntMap.Compare")
	c_IntMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<537>");
	int t_=t_lhs-t_rhs;
	return t_;
}
void c_IntMap::mark(){
	c_Map::mark();
}
String c_IntMap::debug(){
	String t="(IntMap)\n";
	t=c_Map::debug()+t;
	return t;
}
c_Stack::c_Stack(){
	m_data=Array<c_DisplayMode* >();
	m_length=0;
}
c_Stack* c_Stack::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack* c_Stack::m_new2(Array<c_DisplayMode* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack::p_Push(c_DisplayMode* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<67>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<68>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<70>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<71>");
	m_length+=1;
}
void c_Stack::p_Push2(Array<c_DisplayMode* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<79>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<80>");
		p_Push(t_values.At(t_offset+t_i));
	}
}
void c_Stack::p_Push3(Array<c_DisplayMode* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<75>");
	p_Push2(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_DisplayMode* > c_Stack::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<18>");
	Array<c_DisplayMode* > t_t=Array<c_DisplayMode* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_Node::c_Node(){
	m_key=0;
	m_right=0;
	m_left=0;
	m_value=0;
	m_color=0;
	m_parent=0;
}
c_Node* c_Node::m_new(int t_key,c_DisplayMode* t_value,int t_color,c_Node* t_parent){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_color,"color")
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<364>");
	this->m_key=t_key;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<365>");
	gc_assign(this->m_value,t_value);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<366>");
	this->m_color=t_color;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<367>");
	gc_assign(this->m_parent,t_parent);
	return this;
}
c_Node* c_Node::m_new2(){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<361>");
	return this;
}
void c_Node::mark(){
	Object::mark();
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_value);
	gc_mark_q(m_parent);
}
String c_Node::debug(){
	String t="(Node)\n";
	t+=dbg_decl("key",&m_key);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("color",&m_color);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
Array<c_DisplayMode* > bb_app__displayModes;
c_DisplayMode* bb_app__desktopMode;
int bb_app_DeviceWidth(){
	DBG_ENTER("DeviceWidth")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<263>");
	return bb_app__devWidth;
}
int bb_app_DeviceHeight(){
	DBG_ENTER("DeviceHeight")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<267>");
	return bb_app__devHeight;
}
void bb_app_EnumDisplayModes(){
	DBG_ENTER("EnumDisplayModes")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<33>");
	Array<BBDisplayMode* > t_modes=bb_app__game->GetDisplayModes();
	DBG_LOCAL(t_modes,"modes")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<34>");
	c_IntMap* t_mmap=(new c_IntMap)->m_new();
	DBG_LOCAL(t_mmap,"mmap")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<35>");
	c_Stack* t_mstack=(new c_Stack)->m_new();
	DBG_LOCAL(t_mstack,"mstack")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<36>");
	for(int t_i=0;t_i<t_modes.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<37>");
		int t_w=t_modes.At(t_i)->width;
		DBG_LOCAL(t_w,"w")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<38>");
		int t_h=t_modes.At(t_i)->height;
		DBG_LOCAL(t_h,"h")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<39>");
		int t_size=t_w<<16|t_h;
		DBG_LOCAL(t_size,"size")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<40>");
		if(t_mmap->p_Contains(t_size)){
			DBG_BLOCK();
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<42>");
			c_DisplayMode* t_mode=(new c_DisplayMode)->m_new(t_modes.At(t_i)->width,t_modes.At(t_i)->height);
			DBG_LOCAL(t_mode,"mode")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<43>");
			t_mmap->p_Insert(t_size,t_mode);
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<44>");
			t_mstack->p_Push(t_mode);
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<47>");
	gc_assign(bb_app__displayModes,t_mstack->p_ToArray());
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<48>");
	BBDisplayMode* t_mode2=bb_app__game->GetDesktopMode();
	DBG_LOCAL(t_mode2,"mode")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<49>");
	if((t_mode2)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<50>");
		gc_assign(bb_app__desktopMode,(new c_DisplayMode)->m_new(t_mode2->width,t_mode2->height));
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<52>");
		gc_assign(bb_app__desktopMode,(new c_DisplayMode)->m_new(bb_app_DeviceWidth(),bb_app_DeviceHeight()));
	}
}
gxtkGraphics* bb_graphics_renderDevice;
int bb_graphics_SetMatrix(Float t_ix,Float t_iy,Float t_jx,Float t_jy,Float t_tx,Float t_ty){
	DBG_ENTER("SetMatrix")
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_LOCAL(t_jx,"jx")
	DBG_LOCAL(t_jy,"jy")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<312>");
	bb_graphics_context->m_ix=t_ix;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<313>");
	bb_graphics_context->m_iy=t_iy;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<314>");
	bb_graphics_context->m_jx=t_jx;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<315>");
	bb_graphics_context->m_jy=t_jy;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<316>");
	bb_graphics_context->m_tx=t_tx;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<317>");
	bb_graphics_context->m_ty=t_ty;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<318>");
	bb_graphics_context->m_tformed=((t_ix!=FLOAT(1.0) || t_iy!=FLOAT(0.0) || t_jx!=FLOAT(0.0) || t_jy!=FLOAT(1.0) || t_tx!=FLOAT(0.0) || t_ty!=FLOAT(0.0))?1:0);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<319>");
	bb_graphics_context->m_matDirty=1;
	return 0;
}
int bb_graphics_SetMatrix2(Array<Float > t_m){
	DBG_ENTER("SetMatrix")
	DBG_LOCAL(t_m,"m")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<308>");
	bb_graphics_SetMatrix(t_m.At(0),t_m.At(1),t_m.At(2),t_m.At(3),t_m.At(4),t_m.At(5));
	return 0;
}
int bb_graphics_SetColor(Float t_r,Float t_g,Float t_b){
	DBG_ENTER("SetColor")
	DBG_LOCAL(t_r,"r")
	DBG_LOCAL(t_g,"g")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<254>");
	bb_graphics_context->m_color_r=t_r;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<255>");
	bb_graphics_context->m_color_g=t_g;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<256>");
	bb_graphics_context->m_color_b=t_b;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<257>");
	bb_graphics_renderDevice->SetColor(t_r,t_g,t_b);
	return 0;
}
int bb_graphics_SetAlpha(Float t_alpha){
	DBG_ENTER("SetAlpha")
	DBG_LOCAL(t_alpha,"alpha")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<271>");
	bb_graphics_context->m_alpha=t_alpha;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<272>");
	bb_graphics_renderDevice->SetAlpha(t_alpha);
	return 0;
}
int bb_graphics_SetBlend(int t_blend){
	DBG_ENTER("SetBlend")
	DBG_LOCAL(t_blend,"blend")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<280>");
	bb_graphics_context->m_blend=t_blend;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<281>");
	bb_graphics_renderDevice->SetBlend(t_blend);
	return 0;
}
int bb_graphics_SetScissor(Float t_x,Float t_y,Float t_width,Float t_height){
	DBG_ENTER("SetScissor")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<289>");
	bb_graphics_context->m_scissor_x=t_x;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<290>");
	bb_graphics_context->m_scissor_y=t_y;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<291>");
	bb_graphics_context->m_scissor_width=t_width;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<292>");
	bb_graphics_context->m_scissor_height=t_height;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<293>");
	bb_graphics_renderDevice->SetScissor(int(t_x),int(t_y),int(t_width),int(t_height));
	return 0;
}
int bb_graphics_BeginRender(){
	DBG_ENTER("BeginRender")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<225>");
	gc_assign(bb_graphics_renderDevice,bb_graphics_device);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<226>");
	bb_graphics_context->m_matrixSp=0;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<227>");
	bb_graphics_SetMatrix(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<228>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<229>");
	bb_graphics_SetAlpha(FLOAT(1.0));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<230>");
	bb_graphics_SetBlend(0);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<231>");
	bb_graphics_SetScissor(FLOAT(0.0),FLOAT(0.0),Float(bb_app_DeviceWidth()),Float(bb_app_DeviceHeight()));
	return 0;
}
int bb_graphics_EndRender(){
	DBG_ENTER("EndRender")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<235>");
	bb_graphics_renderDevice=0;
	return 0;
}
c_BBGameEvent::c_BBGameEvent(){
}
void c_BBGameEvent::mark(){
	Object::mark();
}
String c_BBGameEvent::debug(){
	String t="(BBGameEvent)\n";
	return t;
}
void bb_app_EndApp(){
	DBG_ENTER("EndApp")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<259>");
	bbError(String());
}
int bb_app__updateRate;
void bb_app_SetUpdateRate(int t_hertz){
	DBG_ENTER("SetUpdateRate")
	DBG_LOCAL(t_hertz,"hertz")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<224>");
	bb_app__updateRate=t_hertz;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/app.monkey<225>");
	bb_app__game->SetUpdateRate(t_hertz);
}
c_Image* bb_main_imgX;
c_Image* bb_main_imgO;
c_NinePatchImage::c_NinePatchImage(){
	m_patches=Array<c_Image* >(9);
	m_w0=0;
	m_w2=0;
	m_h0=0;
	m_h2=0;
	m_xSafe=0;
	m_wSafe=0;
	m_ySafe=0;
	m_hSafe=0;
	m_xOffset=0;
	m_yOffset=0;
	m_wOffset=0;
	m_hOffset=0;
}
c_NinePatchImage* c_NinePatchImage::m_new(c_Image* t_source,Array<int > t_parameters,Array<int > t_safetyParameters){
	DBG_ENTER("NinePatchImage.new")
	c_NinePatchImage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_source,"source")
	DBG_LOCAL(t_parameters,"parameters")
	DBG_LOCAL(t_safetyParameters,"safetyParameters")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<28>");
	int t_w0=t_parameters.At(0);
	DBG_LOCAL(t_w0,"w0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<29>");
	int t_w1=t_parameters.At(1);
	DBG_LOCAL(t_w1,"w1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<30>");
	int t_w2=t_source->p_Width()-t_w0-t_w1;
	DBG_LOCAL(t_w2,"w2")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<32>");
	int t_h0=t_parameters.At(2);
	DBG_LOCAL(t_h0,"h0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<33>");
	int t_h1=t_parameters.At(3);
	DBG_LOCAL(t_h1,"h1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<34>");
	int t_h2=t_source->p_Height()-t_h0-t_h1;
	DBG_LOCAL(t_h2,"h2")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<36>");
	int t_x0=0;
	DBG_LOCAL(t_x0,"x0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<37>");
	int t_x1=t_w0;
	DBG_LOCAL(t_x1,"x1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<38>");
	int t_x2=t_w0+t_w1;
	DBG_LOCAL(t_x2,"x2")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<40>");
	int t_y0=0;
	DBG_LOCAL(t_y0,"y0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<41>");
	int t_y1=t_h0;
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<42>");
	int t_y2=t_h0+t_h1;
	DBG_LOCAL(t_y2,"y2")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<44>");
	gc_assign(m_patches.At(0),t_source->p_GrabImage(t_x0,t_y0,t_w0,t_h0,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<45>");
	gc_assign(m_patches.At(1),t_source->p_GrabImage(t_x1,t_y0,1,t_h0,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<46>");
	gc_assign(m_patches.At(2),t_source->p_GrabImage(t_x2,t_y0,t_w2,t_h0,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<48>");
	gc_assign(m_patches.At(3),t_source->p_GrabImage(t_x0,t_y1,t_w0,1,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<49>");
	gc_assign(m_patches.At(4),t_source->p_GrabImage(t_x1,t_y1,1,1,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<50>");
	gc_assign(m_patches.At(5),t_source->p_GrabImage(t_x2,t_y1,t_w2,1,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<52>");
	gc_assign(m_patches.At(6),t_source->p_GrabImage(t_x0,t_y2,t_w0,t_h2,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<53>");
	gc_assign(m_patches.At(7),t_source->p_GrabImage(t_x1,t_y2,1,t_h2,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<54>");
	gc_assign(m_patches.At(8),t_source->p_GrabImage(t_x2,t_y2,t_w2,t_h2,1,c_Image::m_DefaultFlags));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<56>");
	this->m_w0=t_w0;
	this->m_w2=t_w2;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<57>");
	this->m_h0=t_h0;
	this->m_h2=t_h2;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<59>");
	if(t_safetyParameters.Length()==4){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<60>");
		m_xSafe=t_safetyParameters.At(0);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<61>");
		m_wSafe=t_safetyParameters.At(1)-t_w1;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<62>");
		m_ySafe=t_safetyParameters.At(2);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<63>");
		m_hSafe=t_safetyParameters.At(3)-t_h1;
	}
	return this;
}
c_NinePatchImage* c_NinePatchImage::m_new2(){
	DBG_ENTER("NinePatchImage.new")
	c_NinePatchImage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<9>");
	return this;
}
int c_NinePatchImage::p_minimumWidth(){
	DBG_ENTER("NinePatchImage.minimumWidth")
	c_NinePatchImage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<24>");
	int t_=m_w0+1+m_w2;
	return t_;
}
int c_NinePatchImage::p_minimumHeight(){
	DBG_ENTER("NinePatchImage.minimumHeight")
	c_NinePatchImage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<25>");
	int t_=m_h0+1+m_h2;
	return t_;
}
void c_NinePatchImage::p_Draw(int t_x,int t_y,int t_width,int t_height){
	DBG_ENTER("NinePatchImage.Draw")
	c_NinePatchImage *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<74>");
	t_x-=m_xOffset;
	t_y-=m_yOffset;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<75>");
	t_width+=m_xOffset+m_wOffset;
	t_height+=m_yOffset+m_hOffset;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<77>");
	t_width=bb_math_Max(t_width,p_minimumWidth());
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<78>");
	t_height=bb_math_Max(t_height,p_minimumHeight());
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<80>");
	int t_w1=t_width-m_w0-m_w2;
	DBG_LOCAL(t_w1,"w1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<81>");
	int t_h1=t_height-m_h0-m_h2;
	DBG_LOCAL(t_h1,"h1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<83>");
	int t_x0=t_x;
	DBG_LOCAL(t_x0,"x0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<84>");
	int t_x1=t_x0+m_w0;
	DBG_LOCAL(t_x1,"x1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<85>");
	int t_x2=t_x1+t_w1;
	DBG_LOCAL(t_x2,"x2")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<87>");
	int t_y0=t_y;
	DBG_LOCAL(t_y0,"y0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<88>");
	int t_y1=t_y0+m_h0;
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<89>");
	int t_y2=t_y1+t_h1;
	DBG_LOCAL(t_y2,"y2")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<91>");
	bb_graphics_DrawImage(m_patches.At(0),Float(t_x0),Float(t_y0),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<92>");
	bb_graphics_DrawImage2(m_patches.At(1),Float(t_x1),Float(t_y0),FLOAT(0.0),Float(t_w1),FLOAT(1.0),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<93>");
	bb_graphics_DrawImage(m_patches.At(2),Float(t_x2),Float(t_y0),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<95>");
	bb_graphics_DrawImage2(m_patches.At(3),Float(t_x0),Float(t_y1),FLOAT(0.0),FLOAT(1.0),Float(t_h1),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<96>");
	bb_graphics_DrawImage2(m_patches.At(4),Float(t_x1),Float(t_y1),FLOAT(0.0),Float(t_w1),Float(t_h1),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<97>");
	bb_graphics_DrawImage2(m_patches.At(5),Float(t_x2),Float(t_y1),FLOAT(0.0),FLOAT(1.0),Float(t_h1),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<99>");
	bb_graphics_DrawImage(m_patches.At(6),Float(t_x0),Float(t_y2),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<100>");
	bb_graphics_DrawImage2(m_patches.At(7),Float(t_x1),Float(t_y2),FLOAT(0.0),Float(t_w1),FLOAT(1.0),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/ninepatch.monkey<101>");
	bb_graphics_DrawImage(m_patches.At(8),Float(t_x2),Float(t_y2),0);
}
void c_NinePatchImage::mark(){
	Object::mark();
	gc_mark_q(m_patches);
}
String c_NinePatchImage::debug(){
	String t="(NinePatchImage)\n";
	t+=dbg_decl("patches",&m_patches);
	t+=dbg_decl("w0",&m_w0);
	t+=dbg_decl("w2",&m_w2);
	t+=dbg_decl("h0",&m_h0);
	t+=dbg_decl("h2",&m_h2);
	t+=dbg_decl("xSafe",&m_xSafe);
	t+=dbg_decl("ySafe",&m_ySafe);
	t+=dbg_decl("wSafe",&m_wSafe);
	t+=dbg_decl("hSafe",&m_hSafe);
	t+=dbg_decl("xOffset",&m_xOffset);
	t+=dbg_decl("yOffset",&m_yOffset);
	t+=dbg_decl("wOffset",&m_wOffset);
	t+=dbg_decl("hOffset",&m_hOffset);
	return t;
}
c_NinePatchImage* bb_main_imgTab;
c_Image* bb_main_imgClose;
c_Image* bb_main_imgOpen;
c_Image* bb_main_imgSave;
c_Image* bb_main_imgNew;
c_Template::c_Template(){
	m_name=String();
	m_ins=0;
	m_outs=0;
	m_settings=(new c_StringMap)->m_new();
}
c_Template* c_Template::m_new(String t_name,int t_ins,int t_outs){
	DBG_ENTER("Template.new")
	c_Template *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_ins,"ins")
	DBG_LOCAL(t_outs,"outs")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<80>");
	this->m_name=t_name;
	this->m_ins=t_ins;
	this->m_outs=t_outs;
	return this;
}
c_Template* c_Template::m_new2(){
	DBG_ENTER("Template.new")
	c_Template *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<75>");
	return this;
}
void c_Template::mark(){
	Object::mark();
	gc_mark_q(m_settings);
}
String c_Template::debug(){
	String t="(Template)\n";
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("ins",&m_ins);
	t+=dbg_decl("outs",&m_outs);
	t+=dbg_decl("settings",&m_settings);
	return t;
}
c_List::c_List(){
	m__head=((new c_HeadNode)->m_new());
}
c_List* c_List::m_new(){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node2* c_List::p_AddLast(c_Template* t_data){
	DBG_ENTER("List.AddLast")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node2* t_=(new c_Node2)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List* c_List::m_new2(Array<c_Template* > t_data){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<c_Template* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Template* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast(t_t);
	}
	return this;
}
c_Enumerator* c_List::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<186>");
	c_Enumerator* t_=(new c_Enumerator)->m_new(this);
	return t_;
}
void c_List::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node2::c_Node2(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node2* c_Node2::m_new(c_Node2* t_succ,c_Node2* t_pred,c_Template* t_data){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node2* c_Node2::m_new2(){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
void c_Node2::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node2::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode::c_HeadNode(){
}
c_HeadNode* c_HeadNode::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node2::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode::mark(){
	c_Node2::mark();
}
String c_HeadNode::debug(){
	String t="(HeadNode)\n";
	t=c_Node2::debug()+t;
	return t;
}
c_List* bb_template_templates;
void bb_template_AddTemplate(String t_name,int t_ins,int t_outs){
	DBG_ENTER("AddTemplate")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_ins,"ins")
	DBG_LOCAL(t_outs,"outs")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<50>");
	bb_template_templates->p_AddLast((new c_Template)->m_new(t_name,t_ins,t_outs));
}
c_Enumerator::c_Enumerator(){
	m__list=0;
	m__curr=0;
}
c_Enumerator* c_Enumerator::m_new(c_List* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator* c_Enumerator::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Template* c_Enumerator::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<338>");
	c_Template* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
c_Template* bb_template__GetTemplate(String t_name){
	DBG_ENTER("_GetTemplate")
	DBG_LOCAL(t_name,"name")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<56>");
	c_Enumerator* t_=bb_template_templates->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Template* t_template=t_->p_NextObject();
		DBG_LOCAL(t_template,"template")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<57>");
		if(t_template->m_name==t_name){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<58>");
			return t_template;
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<62>");
	return 0;
}
c_Setting::c_Setting(){
	m_name=String();
	m_kind=String();
	m_value=0;
}
c_Setting* c_Setting::m_new(String t_name,String t_kind,int t_value){
	DBG_ENTER("Setting.new")
	c_Setting *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_kind,"kind")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/setting.monkey<15>");
	this->m_name=t_name;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/setting.monkey<16>");
	this->m_kind=t_kind;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/setting.monkey<17>");
	this->m_value=t_value;
	return this;
}
c_Setting* c_Setting::m_new2(){
	DBG_ENTER("Setting.new")
	c_Setting *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/setting.monkey<9>");
	return this;
}
c_Setting* c_Setting::p_Copy(){
	DBG_ENTER("Setting.Copy")
	c_Setting *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/setting.monkey<21>");
	c_Setting* t_=(new c_Setting)->m_new(m_name,m_kind,m_value);
	return t_;
}
void c_Setting::mark(){
	Object::mark();
}
String c_Setting::debug(){
	String t="(Setting)\n";
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("kind",&m_kind);
	t+=dbg_decl("value",&m_value);
	return t;
}
c_Map2::c_Map2(){
	m_root=0;
}
c_Map2* c_Map2::m_new(){
	DBG_ENTER("Map.new")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<7>");
	return this;
}
int c_Map2::p_RotateLeft2(c_Node3* t_node){
	DBG_ENTER("Map.RotateLeft")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<251>");
	c_Node3* t_child=t_node->m_right;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<252>");
	gc_assign(t_node->m_right,t_child->m_left);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<253>");
	if((t_child->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<254>");
		gc_assign(t_child->m_left->m_parent,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<256>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<257>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<258>");
		if(t_node==t_node->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<259>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<261>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<264>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<266>");
	gc_assign(t_child->m_left,t_node);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<267>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map2::p_RotateRight2(c_Node3* t_node){
	DBG_ENTER("Map.RotateRight")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<271>");
	c_Node3* t_child=t_node->m_left;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<272>");
	gc_assign(t_node->m_left,t_child->m_right);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<273>");
	if((t_child->m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<274>");
		gc_assign(t_child->m_right->m_parent,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<276>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<277>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<278>");
		if(t_node==t_node->m_parent->m_right){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<279>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<281>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<284>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<286>");
	gc_assign(t_child->m_right,t_node);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<287>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map2::p_InsertFixup2(c_Node3* t_node){
	DBG_ENTER("Map.InsertFixup")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<212>");
	while(((t_node->m_parent)!=0) && t_node->m_parent->m_color==-1 && ((t_node->m_parent->m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<213>");
		if(t_node->m_parent==t_node->m_parent->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<214>");
			c_Node3* t_uncle=t_node->m_parent->m_parent->m_right;
			DBG_LOCAL(t_uncle,"uncle")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<215>");
			if(((t_uncle)!=0) && t_uncle->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<216>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<217>");
				t_uncle->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<218>");
				t_uncle->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<219>");
				t_node=t_uncle->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<221>");
				if(t_node==t_node->m_parent->m_right){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<222>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<223>");
					p_RotateLeft2(t_node);
				}
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<225>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<226>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<227>");
				p_RotateRight2(t_node->m_parent->m_parent);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<230>");
			c_Node3* t_uncle2=t_node->m_parent->m_parent->m_left;
			DBG_LOCAL(t_uncle2,"uncle")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<231>");
			if(((t_uncle2)!=0) && t_uncle2->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<232>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<233>");
				t_uncle2->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<234>");
				t_uncle2->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<235>");
				t_node=t_uncle2->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<237>");
				if(t_node==t_node->m_parent->m_left){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<238>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<239>");
					p_RotateRight2(t_node);
				}
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<241>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<242>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<243>");
				p_RotateLeft2(t_node->m_parent->m_parent);
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<247>");
	m_root->m_color=1;
	return 0;
}
bool c_Map2::p_Set2(String t_key,c_Setting* t_value){
	DBG_ENTER("Map.Set")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<29>");
	c_Node3* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<30>");
	c_Node3* t_parent=0;
	int t_cmp=0;
	DBG_LOCAL(t_parent,"parent")
	DBG_LOCAL(t_cmp,"cmp")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<32>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<33>");
		t_parent=t_node;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<34>");
		t_cmp=p_Compare2(t_key,t_node->m_key);
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<35>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<36>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<37>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<38>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<40>");
				gc_assign(t_node->m_value,t_value);
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<41>");
				return false;
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<45>");
	t_node=(new c_Node3)->m_new(t_key,t_value,-1,t_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<47>");
	if((t_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<48>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<49>");
			gc_assign(t_parent->m_right,t_node);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<51>");
			gc_assign(t_parent->m_left,t_node);
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<53>");
		p_InsertFixup2(t_node);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<55>");
		gc_assign(m_root,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<57>");
	return true;
}
bool c_Map2::p_Insert2(String t_key,c_Setting* t_value){
	DBG_ENTER("Map.Insert")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<146>");
	bool t_=p_Set2(t_key,t_value);
	return t_;
}
c_Node3* c_Map2::p_FindNode2(String t_key){
	DBG_ENTER("Map.FindNode")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<157>");
	c_Node3* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<159>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<160>");
		int t_cmp=p_Compare2(t_key,t_node->m_key);
		DBG_LOCAL(t_cmp,"cmp")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<161>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<162>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<163>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<164>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<166>");
				return t_node;
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<169>");
	return t_node;
}
c_Setting* c_Map2::p_Get(String t_key){
	DBG_ENTER("Map.Get")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<101>");
	c_Node3* t_node=p_FindNode2(t_key);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<102>");
	if((t_node)!=0){
		DBG_BLOCK();
		return t_node->m_value;
	}
	return 0;
}
c_MapValues* c_Map2::p_Values(){
	DBG_ENTER("Map.Values")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<117>");
	c_MapValues* t_=(new c_MapValues)->m_new(this);
	return t_;
}
c_Node3* c_Map2::p_FirstNode(){
	DBG_ENTER("Map.FirstNode")
	c_Map2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<125>");
	if(!((m_root)!=0)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<127>");
	c_Node3* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<128>");
	while((t_node->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<129>");
		t_node=t_node->m_left;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<131>");
	return t_node;
}
void c_Map2::mark(){
	Object::mark();
	gc_mark_q(m_root);
}
String c_Map2::debug(){
	String t="(Map)\n";
	t+=dbg_decl("root",&m_root);
	return t;
}
c_StringMap::c_StringMap(){
}
c_StringMap* c_StringMap::m_new(){
	DBG_ENTER("StringMap.new")
	c_StringMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<551>");
	c_Map2::m_new();
	return this;
}
int c_StringMap::p_Compare2(String t_lhs,String t_rhs){
	DBG_ENTER("StringMap.Compare")
	c_StringMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<554>");
	int t_=t_lhs.Compare(t_rhs);
	return t_;
}
void c_StringMap::mark(){
	c_Map2::mark();
}
String c_StringMap::debug(){
	String t="(StringMap)\n";
	t=c_Map2::debug()+t;
	return t;
}
c_Node3::c_Node3(){
	m_key=String();
	m_right=0;
	m_left=0;
	m_value=0;
	m_color=0;
	m_parent=0;
}
c_Node3* c_Node3::m_new(String t_key,c_Setting* t_value,int t_color,c_Node3* t_parent){
	DBG_ENTER("Node.new")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_color,"color")
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<364>");
	this->m_key=t_key;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<365>");
	gc_assign(this->m_value,t_value);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<366>");
	this->m_color=t_color;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<367>");
	gc_assign(this->m_parent,t_parent);
	return this;
}
c_Node3* c_Node3::m_new2(){
	DBG_ENTER("Node.new")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<361>");
	return this;
}
c_Node3* c_Node3::p_NextNode(){
	DBG_ENTER("Node.NextNode")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<385>");
	c_Node3* t_node=0;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<386>");
	if((m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<387>");
		t_node=m_right;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<388>");
		while((t_node->m_left)!=0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<389>");
			t_node=t_node->m_left;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<391>");
		return t_node;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<393>");
	t_node=this;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<394>");
	c_Node3* t_parent=this->m_parent;
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<395>");
	while(((t_parent)!=0) && t_node==t_parent->m_right){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<396>");
		t_node=t_parent;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<397>");
		t_parent=t_parent->m_parent;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<399>");
	return t_parent;
}
void c_Node3::mark(){
	Object::mark();
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_value);
	gc_mark_q(m_parent);
}
String c_Node3::debug(){
	String t="(Node)\n";
	t+=dbg_decl("key",&m_key);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("color",&m_color);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
void bb_template_AddSetting(String t_templateName,String t_name,String t_kind,int t_initial){
	DBG_ENTER("AddSetting")
	DBG_LOCAL(t_templateName,"templateName")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_kind,"kind")
	DBG_LOCAL(t_initial,"initial")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<68>");
	c_Template* t_template=bb_template__GetTemplate(t_templateName);
	DBG_LOCAL(t_template,"template")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<69>");
	c_Setting* t_setting=(new c_Setting)->m_new(t_name,t_kind,t_initial);
	DBG_LOCAL(t_setting,"setting")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<70>");
	t_template->m_settings->p_Insert2(t_name,t_setting);
}
void bb_template_MakeTemplates(){
	DBG_ENTER("MakeTemplates")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<14>");
	int t_tableId=0;
	DBG_LOCAL(t_tableId,"tableId")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<15>");
	bb_template_AddTemplate(String(L"go",2),0,0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<16>");
	bb_template_AddTemplate(String(L"clear",5),0,0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<17>");
	bb_template_AddTemplate(String(L"noise",5),0,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<18>");
	bb_template_AddSetting(String(L"noise",5),String(L"density",7),String(L"f",1),9);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<19>");
	bb_template_AddTemplate(String(L"automata",8),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<20>");
	bb_template_AddSetting(String(L"automata",8),String(L"laps",4),String(L"i1-9",4),1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<21>");
	bb_template_AddSetting(String(L"automata",8),String(L"edge",4),String(L"dedge",5),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<22>");
	bb_template_AddSetting(String(L"automata",8),String(L"rules",5),String(L"a9s8",4),t_tableId);
	t_tableId+=1;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<23>");
	bb_template_AddTemplate(String(L"conway",6),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<24>");
	bb_template_AddTemplate(String(L"smooth",6),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<25>");
	bb_template_AddSetting(String(L"smooth",6),String(L"laps",4),String(L"i1-9",4),1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<26>");
	bb_template_AddSetting(String(L"smooth",6),String(L"edge",4),String(L"dedge",5),0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<27>");
	bb_template_AddTemplate(String(L"expand",6),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<28>");
	bb_template_AddTemplate(String(L"contract",8),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<29>");
	bb_template_AddTemplate(String(L"shift",5),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<30>");
	bb_template_AddTemplate(String(L"skew",4),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<31>");
	bb_template_AddTemplate(String(L"scale",5),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<32>");
	bb_template_AddTemplate(String(L"darken",6),2,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<33>");
	bb_template_AddTemplate(String(L"lighten",7),2,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<34>");
	bb_template_AddTemplate(String(L"invert",6),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<35>");
	bb_template_AddTemplate(String(L"fill",4),0,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<36>");
	bb_template_AddSetting(String(L"fill",4),String(L"color",5),String(L"b",1),1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<37>");
	bb_template_AddTemplate(String(L"canvas",6),0,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<38>");
	bb_template_AddTemplate(String(L"view",4),1,0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<39>");
	bb_template_AddTemplate(String(L"omni",4),0,0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<40>");
	bb_template_AddTemplate(String(L"sequence",8),1,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<41>");
	bb_template_AddTemplate(String(L"patch",5),4,4);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<42>");
	bb_template_AddTemplate(String(L"in",2),0,1);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<43>");
	bb_template_AddTemplate(String(L"out",3),1,0);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/template.monkey<44>");
	bb_template_AddTemplate(String(L"parameter",9),0,1);
}
c_Gadget::c_Gadget(){
	m_x=0;
	m_y=0;
	m_w=0;
	m_h=0;
	m_parent=0;
	m_window=0;
	m__enabled=true;
}
c_Gadget* c_Gadget::m_new(){
	DBG_ENTER("Gadget.new")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<9>");
	return this;
}
int c_Gadget::p_xTranslate(){
	DBG_ENTER("Gadget.xTranslate")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<19>");
	return m_x;
}
int c_Gadget::p__GlobalX(int t__x){
	DBG_ENTER("Gadget._GlobalX")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<50>");
	t__x=t__x+p_xTranslate();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<51>");
	if(m_parent!=0){
		DBG_BLOCK();
		t__x=m_parent->p__GlobalX(t__x);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<52>");
	return t__x;
}
int c_Gadget::p__LocalX(int t__x){
	DBG_ENTER("Gadget._LocalX")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<40>");
	if(m_parent!=0){
		DBG_BLOCK();
		t__x=m_parent->p__LocalX(t__x);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<41>");
	int t_=t__x-p_xTranslate();
	return t_;
}
int c_Gadget::p_GetLocalX(int t_x,c_Gadget* t_from){
	DBG_ENTER("Gadget.GetLocalX")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_from,"from")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<62>");
	int t_=this->p__LocalX(t_from->p__GlobalX(t_x));
	return t_;
}
int c_Gadget::p_yTranslate(){
	DBG_ENTER("Gadget.yTranslate")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<20>");
	return m_y;
}
int c_Gadget::p__GlobalY(int t__y){
	DBG_ENTER("Gadget._GlobalY")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__y,"_y")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<56>");
	t__y=t__y+p_yTranslate();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<57>");
	if(m_parent!=0){
		DBG_BLOCK();
		t__y=m_parent->p__GlobalY(t__y);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<58>");
	return t__y;
}
int c_Gadget::p__LocalY(int t__y){
	DBG_ENTER("Gadget._LocalY")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__y,"_y")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<45>");
	if(m_parent!=0){
		DBG_BLOCK();
		t__y=m_parent->p__LocalY(t__y);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<46>");
	int t_=t__y-p_yTranslate();
	return t_;
}
int c_Gadget::p_GetLocalY(int t_y,c_Gadget* t_from){
	DBG_ENTER("Gadget.GetLocalY")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_from,"from")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<66>");
	int t_=this->p__LocalY(t_from->p__GlobalY(t_y));
	return t_;
}
bool c_Gadget::p_enabled(){
	DBG_ENTER("Gadget.enabled")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<15>");
	return m__enabled;
}
void c_Gadget::p_Disable(){
	DBG_ENTER("Gadget.Disable")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<17>");
	m__enabled=false;
}
void c_Gadget::p_Enable(){
	DBG_ENTER("Gadget.Enable")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<16>");
	m__enabled=true;
}
void c_Gadget::p_OnRender(){
	DBG_ENTER("Gadget.OnRender")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
}
void c_Gadget::p_Render(){
	DBG_ENTER("Gadget.Render")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<31>");
	p_OnRender();
}
void c_Gadget::p_HandleGadgetEvent(c_GadgetEvent* t_event){
	DBG_ENTER("Gadget.HandleGadgetEvent")
	c_Gadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<25>");
	if(m_parent!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadget.monkey<26>");
		m_parent->p_HandleGadgetEvent(t_event);
	}
}
void c_Gadget::mark(){
	Object::mark();
	gc_mark_q(m_parent);
	gc_mark_q(m_window);
}
String c_Gadget::debug(){
	String t="(Gadget)\n";
	t+=dbg_decl("window",&m_window);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("w",&m_w);
	t+=dbg_decl("h",&m_h);
	t+=dbg_decl("_enabled",&m__enabled);
	return t;
}
c_ViewGadget::c_ViewGadget(){
	m_ox=0;
	m_oy=0;
}
c_ViewGadget* c_ViewGadget::m_new(){
	DBG_ENTER("ViewGadget.new")
	c_ViewGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/viewgadget.monkey<9>");
	c_Gadget::m_new();
	return this;
}
void c_ViewGadget::p_OnRenderInterior(){
	DBG_ENTER("ViewGadget.OnRenderInterior")
	c_ViewGadget *self=this;
	DBG_LOCAL(self,"Self")
}
void c_ViewGadget::p_Render(){
	DBG_ENTER("ViewGadget.Render")
	c_ViewGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/viewgadget.monkey<21>");
	p_OnRender();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/viewgadget.monkey<22>");
	bb_graphics_PushMatrix();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/viewgadget.monkey<23>");
	bb_graphics_Translate(Float(m_ox),Float(m_oy));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/viewgadget.monkey<24>");
	p_OnRenderInterior();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/viewgadget.monkey<25>");
	bb_graphics_PopMatrix();
}
int c_ViewGadget::p_xTranslate(){
	DBG_ENTER("ViewGadget.xTranslate")
	c_ViewGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/viewgadget.monkey<13>");
	int t_=m_x+m_ox;
	return t_;
}
int c_ViewGadget::p_yTranslate(){
	DBG_ENTER("ViewGadget.yTranslate")
	c_ViewGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/viewgadget.monkey<17>");
	int t_=m_y+m_oy;
	return t_;
}
void c_ViewGadget::mark(){
	c_Gadget::mark();
}
String c_ViewGadget::debug(){
	String t="(ViewGadget)\n";
	t=c_Gadget::debug()+t;
	t+=dbg_decl("ox",&m_ox);
	t+=dbg_decl("oy",&m_oy);
	return t;
}
c_ContainerGadget::c_ContainerGadget(){
	m_children=(new c_List2)->m_new();
}
c_ContainerGadget* c_ContainerGadget::m_new(int t_x,int t_y,int t_w,int t_h){
	DBG_ENTER("ContainerGadget.new")
	c_ContainerGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<14>");
	c_ViewGadget::m_new();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<15>");
	this->m_x=t_x;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<16>");
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<17>");
	this->m_w=t_w;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<18>");
	this->m_h=t_h;
	return this;
}
c_ContainerGadget* c_ContainerGadget::m_new2(){
	DBG_ENTER("ContainerGadget.new")
	c_ContainerGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<11>");
	c_ViewGadget::m_new();
	return this;
}
void c_ContainerGadget::p__PassDownWindow(){
	DBG_ENTER("ContainerGadget._PassDownWindow")
	c_ContainerGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<30>");
	c_Enumerator2* t_=m_children->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Gadget* t_child=t_->p_NextObject();
		DBG_LOCAL(t_child,"child")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<31>");
		gc_assign(t_child->m_window,m_window);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<32>");
		c_ContainerGadget* t_container=dynamic_cast<c_ContainerGadget*>(t_child);
		DBG_LOCAL(t_container,"container")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<33>");
		if(t_container!=0){
			DBG_BLOCK();
			t_container->p__PassDownWindow();
		}
	}
}
void c_ContainerGadget::p_AddChild(c_Gadget* t_child){
	DBG_ENTER("ContainerGadget.AddChild")
	c_ContainerGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<22>");
	m_children->p_AddLast2(t_child);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<23>");
	gc_assign(t_child->m_parent,this);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<24>");
	gc_assign(t_child->m_window,m_window);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<25>");
	c_ContainerGadget* t_container=dynamic_cast<c_ContainerGadget*>(t_child);
	DBG_LOCAL(t_container,"container")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<26>");
	if(t_container!=0){
		DBG_BLOCK();
		t_container->p__PassDownWindow();
	}
}
c_Gadget* c_ContainerGadget::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("ContainerGadget.HandleEvent")
	c_ContainerGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<38>");
	if(t_event->m_destination==(this)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<39>");
		c_Gadget* t_=(this);
		return t_;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<42>");
	if(t_event->m_destination!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<43>");
		t_event->m_x=t_event->m_destination->p_GetLocalX(t_event->m_x,(this));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<44>");
		t_event->m_y=t_event->m_destination->p_GetLocalY(t_event->m_y,(this));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<45>");
		c_Gadget* t_2=t_event->m_destination->p_HandleEvent(t_event);
		return t_2;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<47>");
		c_Gadget* t_champ=0;
		DBG_LOCAL(t_champ,"champ")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<49>");
		c_BackwardsEnumerator* t_3=m_children->p_Backwards()->p_ObjectEnumerator();
		while(t_3->p_HasNext()){
			DBG_BLOCK();
			c_Gadget* t_child=t_3->p_NextObject();
			DBG_LOCAL(t_child,"child")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<50>");
			if(!t_child->p_enabled()){
				DBG_BLOCK();
				continue;
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<52>");
			if((bb_gui_RectangleContainsPoint(t_child->m_x,t_child->m_y,t_child->m_w,t_child->m_h,t_event->m_x,t_event->m_y))!=0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<53>");
				t_champ=t_child;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<54>");
				break;
			}
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<58>");
		if(t_champ!=0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<59>");
			t_event->m_x=t_champ->p_GetLocalX(t_event->m_x,(this));
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<60>");
			t_event->m_y=t_champ->p_GetLocalY(t_event->m_y,(this));
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<61>");
			gc_assign(t_event->m_destination,t_champ->p_HandleEvent(t_event));
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<65>");
	c_Gadget* t_out=t_event->m_destination;
	DBG_LOCAL(t_out,"out")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<67>");
	if(t_out==0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<68>");
		t_out=(this);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<71>");
	return t_out;
}
void c_ContainerGadget::p_OnRender(){
	DBG_ENTER("ContainerGadget.OnRender")
	c_ContainerGadget *self=this;
	DBG_LOCAL(self,"Self")
}
void c_ContainerGadget::p_OnRenderInterior(){
	DBG_ENTER("ContainerGadget.OnRenderInterior")
	c_ContainerGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<79>");
	bb_graphics_PushMatrix();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<80>");
	bb_gui_PushScissor();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<82>");
	Array<Float > t__o=bb_graphics_GetMatrix();
	DBG_LOCAL(t__o,"_o")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<83>");
	int t_ox=int(t__o.At(4));
	int t_oy=int(t__o.At(5));
	DBG_LOCAL(t_ox,"ox")
	DBG_LOCAL(t_oy,"oy")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<84>");
	Array<Float > t__s=bb_graphics_GetScissor();
	DBG_LOCAL(t__s,"_s")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<85>");
	int t_sx=int(t__s.At(0));
	int t_sy=int(t__s.At(1));
	int t_sw=int(t__s.At(2));
	int t_sh=int(t__s.At(3));
	DBG_LOCAL(t_sx,"sx")
	DBG_LOCAL(t_sy,"sy")
	DBG_LOCAL(t_sw,"sw")
	DBG_LOCAL(t_sh,"sh")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<87>");
	c_Enumerator2* t_=m_children->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Gadget* t_child=t_->p_NextObject();
		DBG_LOCAL(t_child,"child")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<88>");
		if(!t_child->p_enabled()){
			DBG_BLOCK();
			continue;
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<89>");
		bb_graphics_PushMatrix();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<90>");
		bb_gui_PushScissor();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<91>");
		int t_cx=t_child->m_x+t_ox;
		int t_cy=t_child->m_y+t_oy;
		int t_cw=t_child->m_w;
		int t_ch=t_child->m_h;
		DBG_LOCAL(t_cx,"cx")
		DBG_LOCAL(t_cy,"cy")
		DBG_LOCAL(t_cw,"cw")
		DBG_LOCAL(t_ch,"ch")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<92>");
		Array<int > t_s=bb_gui_RectangleUnion(t_sx,t_sy,t_sw,t_sh,t_cx,t_cy,t_cw,t_ch);
		DBG_LOCAL(t_s,"s")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<93>");
		bb_graphics_SetScissor(Float(t_s.At(0)),Float(t_s.At(1)),Float(t_s.At(2)),Float(t_s.At(3)));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<94>");
		bb_graphics_Translate(Float(t_child->m_x),Float(t_child->m_y));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<95>");
		t_child->p_Render();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<96>");
		bb_graphics_PopMatrix();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<97>");
		bb_gui_PopScissor();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<100>");
	bb_graphics_PopMatrix();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/containergadget.monkey<101>");
	bb_gui_PopScissor();
}
void c_ContainerGadget::mark(){
	c_ViewGadget::mark();
	gc_mark_q(m_children);
}
String c_ContainerGadget::debug(){
	String t="(ContainerGadget)\n";
	t=c_ViewGadget::debug()+t;
	t+=dbg_decl("children",&m_children);
	return t;
}
c_WindowGadget::c_WindowGadget(){
	m__mouseX=0;
	m__mousePreviousX=0;
	m__mouseY=0;
	m__mousePreviousY=0;
	m__mouseState=0;
	m__mouseDragX=0;
	m__mouseDragY=0;
	m__destination=0;
	m__events=(new c_List3)->m_new();
	m__mousePreviousState=0;
}
c_WindowGadget* c_WindowGadget::m_new(int t_x,int t_y,int t_w,int t_h){
	DBG_ENTER("WindowGadget.new")
	c_WindowGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<19>");
	c_ContainerGadget::m_new(t_x,t_y,t_w,t_h);
	return this;
}
c_WindowGadget* c_WindowGadget::m_new2(){
	DBG_ENTER("WindowGadget.new")
	c_WindowGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<9>");
	c_ContainerGadget::m_new2();
	return this;
}
void c_WindowGadget::p_AddChild(c_Gadget* t_child){
	DBG_ENTER("WindowGadget.AddChild")
	c_WindowGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<23>");
	m_children->p_AddLast2(t_child);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<24>");
	gc_assign(t_child->m_parent,(this));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<25>");
	gc_assign(t_child->m_window,this);
}
void c_WindowGadget::p_Update(){
	DBG_ENTER("WindowGadget.Update")
	c_WindowGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<41>");
	m__mousePreviousX=m__mouseX;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<42>");
	m__mousePreviousY=m__mouseY;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<44>");
	m__mouseX=int(bb_input_MouseX());
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<45>");
	m__mouseY=int(bb_input_MouseY());
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<47>");
	if(m__mouseX!=m__mousePreviousX || m__mouseY!=m__mousePreviousY){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<48>");
		int t_state=m__mouseState;
		DBG_LOCAL(t_state,"state")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<49>");
		if(t_state>=3){
			DBG_BLOCK();
			t_state-=2;
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<50>");
		int t_[]={0,3,4};
		m__events->p_AddLast3((new c_Event)->m_new(Array<int >(t_,3).At(t_state),c_Event::m_globalWindow));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<53>");
	m__mousePreviousState=m__mouseState;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<55>");
	if(((bb_input_MouseDown(0))!=0) && ((bb_input_MouseDown(1))!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<56>");
		int t_2=m__mousePreviousState;
		DBG_LOCAL(t_2,"2")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<57>");
		if(t_2==bb_event_MOUSE_STATE_NONE || t_2==bb_event_MOUSE_STATE_RIGHT || t_2==bb_event_MOUSE_STATE_BOTH_LEFT || t_2==bb_event_MOUSE_STATE_BOTH_RIGHT){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<58>");
			m__mouseState=bb_event_MOUSE_STATE_BOTH_LEFT;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<59>");
			if(t_2==bb_event_MOUSE_STATE_LEFT){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<60>");
				m__mouseState=bb_event_MOUSE_STATE_BOTH_RIGHT;
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<63>");
		if((bb_input_MouseDown(0))!=0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<64>");
			m__mouseState=bb_event_MOUSE_STATE_LEFT;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<65>");
			if((bb_input_MouseDown(1))!=0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<66>");
				m__mouseState=bb_event_MOUSE_STATE_RIGHT;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<68>");
				m__mouseState=bb_event_MOUSE_STATE_NONE;
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<71>");
	int t_3[]={-2,-2};
	Array<int > t_id=Array<int >(t_3,2);
	DBG_LOCAL(t_id,"id")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<73>");
	int t_32=m__mouseState;
	DBG_LOCAL(t_32,"3")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<74>");
	if(t_32==bb_event_MOUSE_STATE_NONE){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<75>");
		int t_4=m__mousePreviousState;
		DBG_LOCAL(t_4,"4")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<76>");
		if(t_4==bb_event_MOUSE_STATE_NONE){
			DBG_BLOCK();
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<78>");
			if(t_4==bb_event_MOUSE_STATE_LEFT || t_4==bb_event_MOUSE_STATE_BOTH_LEFT){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<79>");
				t_id.At(0)=5;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<80>");
				if(t_4==bb_event_MOUSE_STATE_RIGHT || t_4==bb_event_MOUSE_STATE_BOTH_RIGHT){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<81>");
					t_id.At(0)=6;
				}
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<84>");
		if(t_32==bb_event_MOUSE_STATE_LEFT || t_32==bb_event_MOUSE_STATE_BOTH_LEFT){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<85>");
			int t_5=m__mousePreviousState;
			DBG_LOCAL(t_5,"5")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<86>");
			if(t_5==bb_event_MOUSE_STATE_NONE){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<87>");
				t_id.At(0)=1;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<88>");
				if(t_5==bb_event_MOUSE_STATE_LEFT || t_5==bb_event_MOUSE_STATE_BOTH_LEFT){
					DBG_BLOCK();
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<90>");
					if(t_5==bb_event_MOUSE_STATE_RIGHT || t_5==bb_event_MOUSE_STATE_BOTH_RIGHT){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<91>");
						t_id.At(0)=6;
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<92>");
						t_id.At(1)=1;
					}
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<95>");
			if(t_32==bb_event_MOUSE_STATE_RIGHT || t_32==bb_event_MOUSE_STATE_BOTH_RIGHT){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<96>");
				int t_6=m__mousePreviousState;
				DBG_LOCAL(t_6,"6")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<97>");
				if(t_6==bb_event_MOUSE_STATE_NONE){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<98>");
					t_id.At(0)=2;
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<99>");
					if(t_6==bb_event_MOUSE_STATE_LEFT || t_6==bb_event_MOUSE_STATE_BOTH_LEFT){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<100>");
						t_id.At(0)=5;
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<101>");
						t_id.At(1)=2;
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<102>");
						if(t_6==bb_event_MOUSE_STATE_RIGHT || t_6==bb_event_MOUSE_STATE_BOTH_RIGHT){
							DBG_BLOCK();
						}
					}
				}
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<109>");
	for(int t_n=0;t_n<=1;t_n=t_n+1){
		DBG_BLOCK();
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<110>");
		if(t_id.At(t_n)!=-2){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<111>");
			m__events->p_AddLast3((new c_Event)->m_new(t_id.At(t_n),c_Event::m_globalWindow));
		}
	}
}
c_Gadget* c_WindowGadget::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("WindowGadget.HandleEvent")
	c_WindowGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<29>");
	c_Gadget* t_destination=c_ContainerGadget::p_HandleEvent(t_event);
	DBG_LOCAL(t_destination,"destination")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<30>");
	int t_1=t_event->m_id;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<31>");
	if(t_1==1 || t_1==2){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<32>");
		m__mouseDragX=m__mouseX;
		m__mouseDragY=m__mouseY;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<33>");
		gc_assign(m__destination,t_destination);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/windowgadget.monkey<37>");
	return t_destination;
}
void c_WindowGadget::mark(){
	c_ContainerGadget::mark();
	gc_mark_q(m__destination);
	gc_mark_q(m__events);
}
String c_WindowGadget::debug(){
	String t="(WindowGadget)\n";
	t=c_ContainerGadget::debug()+t;
	t+=dbg_decl("_mouseX",&m__mouseX);
	t+=dbg_decl("_mouseY",&m__mouseY);
	t+=dbg_decl("_mousePreviousX",&m__mousePreviousX);
	t+=dbg_decl("_mousePreviousY",&m__mousePreviousY);
	t+=dbg_decl("_mouseDragX",&m__mouseDragX);
	t+=dbg_decl("_mouseDragY",&m__mouseDragY);
	t+=dbg_decl("_mouseState",&m__mouseState);
	t+=dbg_decl("_mousePreviousState",&m__mousePreviousState);
	t+=dbg_decl("_destination",&m__destination);
	t+=dbg_decl("_events",&m__events);
	return t;
}
c_Event::c_Event(){
	m_id=0;
	m_window=0;
	m_x=0;
	m_y=0;
	m_dx=0;
	m_dy=0;
	m_destination=0;
}
c_WindowGadget* c_Event::m_globalWindow;
c_Event* c_Event::m_new(int t_id,c_WindowGadget* t_window){
	DBG_ENTER("Event.new")
	c_Event *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_id,"id")
	DBG_LOCAL(t_window,"window")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/event.monkey<43>");
	this->m_id=t_id;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/event.monkey<44>");
	gc_assign(this->m_window,t_window);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/event.monkey<45>");
	m_x=t_window->m__mouseX;
	m_y=t_window->m__mouseY;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/event.monkey<47>");
	int t_1=t_id;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/event.monkey<48>");
	if(t_1==3 || t_1==4 || t_1==5 || t_1==6){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/event.monkey<49>");
		m_dx=m_x-t_window->m__mouseDragX;
		m_dy=m_y-t_window->m__mouseDragY;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/event.monkey<50>");
		gc_assign(m_destination,t_window->m__destination);
	}
	return this;
}
c_Event* c_Event::m_new2(){
	DBG_ENTER("Event.new")
	c_Event *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/event.monkey<33>");
	return this;
}
void c_Event::mark(){
	Object::mark();
	gc_mark_q(m_window);
	gc_mark_q(m_destination);
}
String c_Event::debug(){
	String t="(Event)\n";
	t+=dbg_decl("globalWindow",&c_Event::m_globalWindow);
	t+=dbg_decl("window",&m_window);
	t+=dbg_decl("id",&m_id);
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("dx",&m_dx);
	t+=dbg_decl("dy",&m_dy);
	t+=dbg_decl("destination",&m_destination);
	return t;
}
c_Browser::c_Browser(){
	m_openButton=0;
	m_newButton=0;
	m_tabs=(new c_List8)->m_new();
}
c_Browser* c_Browser::m_new(int t_x,int t_y,int t_w,int t_h){
	DBG_ENTER("Browser.new")
	c_Browser *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<32>");
	c_ContainerGadget::m_new(t_x,t_y,t_w,t_h);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<33>");
	gc_assign(m_openButton,(new c_BrowserButton)->m_new(0,5,bb_main_imgOpen));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<34>");
	gc_assign(m_newButton,(new c_BrowserButton)->m_new(0,5,bb_main_imgNew));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<35>");
	p_AddChild(m_openButton);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<36>");
	p_AddChild(m_newButton);
	return this;
}
c_Browser* c_Browser::m_new2(){
	DBG_ENTER("Browser.new")
	c_Browser *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<26>");
	c_ContainerGadget::m_new2();
	return this;
}
void c_Browser::p_OnRender(){
	DBG_ENTER("Browser.OnRender")
	c_Browser *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<87>");
	int t_x=4;
	DBG_LOCAL(t_x,"x")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<89>");
	if(!m_tabs->p_IsEmpty()){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<90>");
		c_List8* t_tabsNew=(new c_List8)->m_new();
		DBG_LOCAL(t_tabsNew,"tabsNew")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<92>");
		do{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<93>");
			c_TabGadget* t_champ=0;
			int t_champX=9999;
			DBG_LOCAL(t_champ,"champ")
			DBG_LOCAL(t_champX,"champX")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<95>");
			c_Enumerator6* t_=m_tabs->p_ObjectEnumerator();
			while(t_->p_HasNext()){
				DBG_BLOCK();
				c_TabGadget* t_tab=t_->p_NextObject();
				DBG_LOCAL(t_tab,"tab")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<96>");
				if(t_tab->m_x<t_champX){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<97>");
					t_champ=t_tab;
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<98>");
					t_champX=t_tab->m_x;
				}
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<102>");
			m_tabs->p_RemoveEach5(t_champ);
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<103>");
			t_tabsNew->p_AddFirst2(t_champ);
		}while(!(m_tabs->p_IsEmpty()));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<106>");
		gc_assign(m_tabs,t_tabsNew);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<107>");
		bb_main_APP->p_project2(0);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<109>");
		c_BackwardsEnumerator2* t_2=m_tabs->p_Backwards()->p_ObjectEnumerator();
		while(t_2->p_HasNext()){
			DBG_BLOCK();
			c_TabGadget* t_tab2=t_2->p_NextObject();
			DBG_LOCAL(t_tab2,"tab")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<110>");
			if(t_tab2->m_locked){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<111>");
				t_tab2->m_x=t_x;
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<114>");
			if(dynamic_cast<c_TabGadget*>(m_children->p_Last())==t_tab2){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<115>");
				t_tab2->m_chosen=true;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<116>");
				bb_main_APP->p_project2(t_tab2->m_project);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<118>");
				t_tab2->m_chosen=false;
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<120>");
			t_x+=t_tab2->m_w-7;
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<124>");
	m_openButton->m_x=t_x+11;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<125>");
	m_newButton->m_x=t_x+11+9+5;
}
void c_Browser::p_OnRenderInterior(){
	DBG_ENTER("Browser.OnRenderInterior")
	c_Browser *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<129>");
	c_ContainerGadget::p_OnRenderInterior();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<130>");
	bb_graphics_PopMatrix();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<131>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<132>");
	bb_graphics_DrawLine(FLOAT(0.0),Float(m_h-1),Float(m_w),Float(m_h-1));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<134>");
	c_TabGadget* t_top=dynamic_cast<c_TabGadget*>(m_children->p_Last());
	DBG_LOCAL(t_top,"top")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<136>");
	if(t_top!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<137>");
		bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<138>");
		bb_graphics_DrawLine(Float(t_top->m_x+1),Float(m_h-1),Float(t_top->m_x+t_top->m_w-1),Float(m_h-1));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<141>");
	bb_graphics_PushMatrix();
}
void c_Browser::p_RemoveTab(c_TabGadget* t_tab){
	DBG_ENTER("Browser.RemoveTab")
	c_Browser *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_tab,"tab")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<47>");
	m_tabs->p_RemoveEach5(t_tab);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<48>");
	m_children->p_RemoveEach(t_tab);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<49>");
	m_window->m_children->p_Remove(t_tab->m_project);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<50>");
	if(bb_main_APP->p_project()==t_tab->m_project){
		DBG_BLOCK();
		bb_main_APP->p_project2(0);
	}
}
void c_Browser::p_AddTab(String t_name){
	DBG_ENTER("Browser.AddTab")
	c_Browser *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_name,"name")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<40>");
	c_TabGadget* t_tab=(new c_TabGadget)->m_new(t_name);
	DBG_LOCAL(t_tab,"tab")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<41>");
	if(!m_tabs->p_IsEmpty()){
		DBG_BLOCK();
		t_tab->m_x=m_tabs->p_First()->m_x+1;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<42>");
	p_AddChild(t_tab);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<43>");
	m_tabs->p_AddFirst2(t_tab);
}
void c_Browser::p_HandleGadgetEvent(c_GadgetEvent* t_event){
	DBG_ENTER("Browser.HandleGadgetEvent")
	c_Browser *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<54>");
	if(dynamic_cast<c_TabGadget*>(t_event->m_source)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<55>");
		c_TabGadget* t_top=dynamic_cast<c_TabGadget*>(m_children->p_Last());
		DBG_LOCAL(t_top,"top")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<56>");
		if((t_top)==t_event->m_source){
			DBG_BLOCK();
			return;
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<57>");
		m_children->p_RemoveLast();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<59>");
		c_TabGadget* t_tabPrevious=0;
		DBG_LOCAL(t_tabPrevious,"tabPrevious")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<61>");
		c_Enumerator6* t_=m_tabs->p_ObjectEnumerator();
		while(t_->p_HasNext()){
			DBG_BLOCK();
			c_TabGadget* t_tab=t_->p_NextObject();
			DBG_LOCAL(t_tab,"tab")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<62>");
			if(t_tab==t_top){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<63>");
				break;
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<66>");
			t_tabPrevious=t_tab;
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<69>");
		if(t_tabPrevious==0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<70>");
			m_children->p_AddFirst(t_top);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<72>");
			m_children->p_InsertAfter((t_tabPrevious),(t_top));
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<75>");
		m_children->p_RemoveEach(t_event->m_source);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<76>");
		m_children->p_AddLast2(t_event->m_source);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<77>");
		if(t_event->m_source==(m_openButton)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<78>");
			String t_path=diddy::openfilename();
			DBG_LOCAL(t_path,"path")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<79>");
			if(t_path!=String()){
				DBG_BLOCK();
				p_AddTab(t_path);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<82>");
			p_AddTab(String());
		}
	}
}
void c_Browser::mark(){
	c_ContainerGadget::mark();
	gc_mark_q(m_openButton);
	gc_mark_q(m_newButton);
	gc_mark_q(m_tabs);
}
String c_Browser::debug(){
	String t="(Browser)\n";
	t=c_ContainerGadget::debug()+t;
	t+=dbg_decl("tabs",&m_tabs);
	t+=dbg_decl("openButton",&m_openButton);
	t+=dbg_decl("newButton",&m_newButton);
	return t;
}
c_ButtonGadget::c_ButtonGadget(){
	m_state=0;
}
c_ButtonGadget* c_ButtonGadget::m_new(int t_x,int t_y){
	DBG_ENTER("ButtonGadget.new")
	c_ButtonGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<150>");
	c_Gadget::m_new();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<151>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<152>");
	this->m_w=16;
	this->m_h=16;
	return this;
}
c_ButtonGadget* c_ButtonGadget::m_new2(){
	DBG_ENTER("ButtonGadget.new")
	c_ButtonGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<147>");
	c_Gadget::m_new();
	return this;
}
c_Gadget* c_ButtonGadget::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("ButtonGadget.HandleEvent")
	c_ButtonGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<156>");
	int t_3=t_event->m_id;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<157>");
	if(t_3==5){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<158>");
		c_Gadget* t_gadget=bb_main_APP->m_window->p_HandleEvent((new c_Event)->m_new(-1,c_Event::m_globalWindow));
		DBG_LOCAL(t_gadget,"gadget")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<160>");
		if(t_gadget==(this)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<161>");
			m_parent->p_HandleGadgetEvent((new c_GadgetEvent)->m_new(this));
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<164>");
		m_state=0;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<165>");
		if(t_3==1){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<166>");
			m_state=1;
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<170>");
	c_Gadget* t_=(this);
	return t_;
}
void c_ButtonGadget::p_OnRender(){
	DBG_ENTER("ButtonGadget.OnRender")
	c_ButtonGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<174>");
	bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<175>");
	bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<176>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<177>");
	bb_graphics_DrawRect(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<178>");
	bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<179>");
	bb_graphics_DrawRect(FLOAT(4.0),FLOAT(4.0),Float(m_w-8),Float(m_h-8));
}
void c_ButtonGadget::mark(){
	c_Gadget::mark();
}
String c_ButtonGadget::debug(){
	String t="(ButtonGadget)\n";
	t=c_Gadget::debug()+t;
	t+=dbg_decl("state",&m_state);
	return t;
}
c_BrowserButton::c_BrowserButton(){
	m_image=0;
}
c_BrowserButton* c_BrowserButton::m_new(int t_x,int t_y,c_Image* t_image){
	DBG_ENTER("BrowserButton.new")
	c_BrowserButton *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_image,"image")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<12>");
	c_ButtonGadget::m_new2();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<13>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<14>");
	m_w=9;
	m_h=9;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<15>");
	gc_assign(this->m_image,t_image);
	return this;
}
c_BrowserButton* c_BrowserButton::m_new2(){
	DBG_ENTER("BrowserButton.new")
	c_BrowserButton *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<9>");
	c_ButtonGadget::m_new2();
	return this;
}
void c_BrowserButton::p_OnRender(){
	DBG_ENTER("BrowserButton.OnRender")
	c_BrowserButton *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<19>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<20>");
	bb_graphics_DrawImage(m_image,FLOAT(0.0),FLOAT(0.0),0);
}
void c_BrowserButton::mark(){
	c_ButtonGadget::mark();
	gc_mark_q(m_image);
}
String c_BrowserButton::debug(){
	String t="(BrowserButton)\n";
	t=c_ButtonGadget::debug()+t;
	t+=dbg_decl("image",&m_image);
	return t;
}
c_List2::c_List2(){
	m__head=((new c_HeadNode2)->m_new());
}
c_List2* c_List2::m_new(){
	DBG_ENTER("List.new")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node4* c_List2::p_AddLast2(c_Gadget* t_data){
	DBG_ENTER("List.AddLast")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node4* t_=(new c_Node4)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List2* c_List2::m_new2(Array<c_Gadget* > t_data){
	DBG_ENTER("List.new")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<c_Gadget* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Gadget* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast2(t_t);
	}
	return this;
}
c_Enumerator2* c_List2::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<186>");
	c_Enumerator2* t_=(new c_Enumerator2)->m_new(this);
	return t_;
}
c_BackwardsList* c_List2::p_Backwards(){
	DBG_ENTER("List.Backwards")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<190>");
	c_BackwardsList* t_=(new c_BackwardsList)->m_new(this);
	return t_;
}
bool c_List2::p_IsEmpty(){
	DBG_ENTER("List.IsEmpty")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<50>");
	bool t_=m__head->m__succ==m__head;
	return t_;
}
c_Gadget* c_List2::p_Last(){
	DBG_ENTER("List.Last")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<80>");
	if(p_IsEmpty()){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on empty list",31));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<82>");
	return m__head->m__pred->m__data;
}
int c_List2::p_Clear(){
	DBG_ENTER("List.Clear")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<36>");
	gc_assign(m__head->m__succ,m__head);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<37>");
	gc_assign(m__head->m__pred,m__head);
	return 0;
}
bool c_List2::p_Equals(c_Gadget* t_lhs,c_Gadget* t_rhs){
	DBG_ENTER("List.Equals")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
int c_List2::p_RemoveEach(c_Gadget* t_value){
	DBG_ENTER("List.RemoveEach")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<151>");
	c_Node4* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<152>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<153>");
		c_Node4* t_succ=t_node->m__succ;
		DBG_LOCAL(t_succ,"succ")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<154>");
		if(p_Equals(t_node->m__data,t_value)){
			DBG_BLOCK();
			t_node->p_Remove2();
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<155>");
		t_node=t_succ;
	}
	return 0;
}
void c_List2::p_Remove(c_Gadget* t_value){
	DBG_ENTER("List.Remove")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<137>");
	p_RemoveEach(t_value);
}
c_Gadget* c_List2::p_RemoveLast(){
	DBG_ENTER("List.RemoveLast")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<96>");
	if(p_IsEmpty()){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on empty list",31));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<98>");
	c_Gadget* t_data=m__head->m__pred->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<99>");
	m__head->m__pred->p_Remove2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<100>");
	return t_data;
}
c_Node4* c_List2::p_FindLast(c_Gadget* t_value,c_Node4* t_start){
	DBG_ENTER("List.FindLast")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_start,"start")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<128>");
	while(t_start!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<129>");
		if(p_Equals(t_value,t_start->m__data)){
			DBG_BLOCK();
			return t_start;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<130>");
		t_start=t_start->m__pred;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<132>");
	return 0;
}
c_Node4* c_List2::p_FindLast2(c_Gadget* t_value){
	DBG_ENTER("List.FindLast")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<124>");
	c_Node4* t_=p_FindLast(t_value,m__head->m__pred);
	return t_;
}
void c_List2::p_RemoveLast2(c_Gadget* t_value){
	DBG_ENTER("List.RemoveLast")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<146>");
	c_Node4* t_node=p_FindLast2(t_value);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<147>");
	if((t_node)!=0){
		DBG_BLOCK();
		t_node->p_Remove2();
	}
}
c_Node4* c_List2::p_AddFirst(c_Gadget* t_data){
	DBG_ENTER("List.AddFirst")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<104>");
	c_Node4* t_=(new c_Node4)->m_new(m__head->m__succ,m__head,t_data);
	return t_;
}
c_Node4* c_List2::p_Find(c_Gadget* t_value,c_Node4* t_start){
	DBG_ENTER("List.Find")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_start,"start")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<116>");
	while(t_start!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<117>");
		if(p_Equals(t_value,t_start->m__data)){
			DBG_BLOCK();
			return t_start;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<118>");
		t_start=t_start->m__succ;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<120>");
	return 0;
}
c_Node4* c_List2::p_Find2(c_Gadget* t_value){
	DBG_ENTER("List.Find")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<112>");
	c_Node4* t_=p_Find(t_value,m__head->m__succ);
	return t_;
}
c_Node4* c_List2::p_InsertAfter(c_Gadget* t_where,c_Gadget* t_data){
	DBG_ENTER("List.InsertAfter")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_where,"where")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<165>");
	c_Node4* t_node=p_Find2(t_where);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<166>");
	if((t_node)!=0){
		DBG_BLOCK();
		c_Node4* t_=(new c_Node4)->m_new(t_node->m__succ,t_node,t_data);
		return t_;
	}
	return 0;
}
void c_List2::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List2::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node4::c_Node4(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node4* c_Node4::m_new(c_Node4* t_succ,c_Node4* t_pred,c_Gadget* t_data){
	DBG_ENTER("Node.new")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node4* c_Node4::m_new2(){
	DBG_ENTER("Node.new")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node4::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node4::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node4::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode2::c_HeadNode2(){
}
c_HeadNode2* c_HeadNode2::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node4::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode2::mark(){
	c_Node4::mark();
}
String c_HeadNode2::debug(){
	String t="(HeadNode)\n";
	t=c_Node4::debug()+t;
	return t;
}
c_Enumerator2::c_Enumerator2(){
	m__list=0;
	m__curr=0;
}
c_Enumerator2* c_Enumerator2::m_new(c_List2* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator2* c_Enumerator2::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator2::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Gadget* c_Enumerator2::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<338>");
	c_Gadget* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator2::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator2::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
Float bb_input_MouseX(){
	DBG_ENTER("MouseX")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/input.monkey<58>");
	Float t_=bb_input_device->p_MouseX();
	return t_;
}
Float bb_input_MouseY(){
	DBG_ENTER("MouseY")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/input.monkey<62>");
	Float t_=bb_input_device->p_MouseY();
	return t_;
}
c_List3::c_List3(){
	m__head=((new c_HeadNode3)->m_new());
}
c_List3* c_List3::m_new(){
	DBG_ENTER("List.new")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node5* c_List3::p_AddLast3(c_Event* t_data){
	DBG_ENTER("List.AddLast")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node5* t_=(new c_Node5)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List3* c_List3::m_new2(Array<c_Event* > t_data){
	DBG_ENTER("List.new")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<c_Event* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Event* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast3(t_t);
	}
	return this;
}
int c_List3::p_Count(){
	DBG_ENTER("List.Count")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<41>");
	int t_n=0;
	c_Node5* t_node=m__head->m__succ;
	DBG_LOCAL(t_n,"n")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<42>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<43>");
		t_node=t_node->m__succ;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<44>");
		t_n+=1;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<46>");
	return t_n;
}
bool c_List3::p_IsEmpty(){
	DBG_ENTER("List.IsEmpty")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<50>");
	bool t_=m__head->m__succ==m__head;
	return t_;
}
c_Event* c_List3::p_RemoveFirst(){
	DBG_ENTER("List.RemoveFirst")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<87>");
	if(p_IsEmpty()){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on empty list",31));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<89>");
	c_Event* t_data=m__head->m__succ->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<90>");
	m__head->m__succ->p_Remove2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<91>");
	return t_data;
}
bool c_List3::p_Equals2(c_Event* t_lhs,c_Event* t_rhs){
	DBG_ENTER("List.Equals")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
c_Node5* c_List3::p_Find3(c_Event* t_value,c_Node5* t_start){
	DBG_ENTER("List.Find")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_start,"start")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<116>");
	while(t_start!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<117>");
		if(p_Equals2(t_value,t_start->m__data)){
			DBG_BLOCK();
			return t_start;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<118>");
		t_start=t_start->m__succ;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<120>");
	return 0;
}
c_Node5* c_List3::p_Find4(c_Event* t_value){
	DBG_ENTER("List.Find")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<112>");
	c_Node5* t_=p_Find3(t_value,m__head->m__succ);
	return t_;
}
void c_List3::p_RemoveFirst2(c_Event* t_value){
	DBG_ENTER("List.RemoveFirst")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<141>");
	c_Node5* t_node=p_Find4(t_value);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<142>");
	if((t_node)!=0){
		DBG_BLOCK();
		t_node->p_Remove2();
	}
}
void c_List3::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List3::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node5::c_Node5(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node5* c_Node5::m_new(c_Node5* t_succ,c_Node5* t_pred,c_Event* t_data){
	DBG_ENTER("Node.new")
	c_Node5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node5* c_Node5::m_new2(){
	DBG_ENTER("Node.new")
	c_Node5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node5::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node5::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node5::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode3::c_HeadNode3(){
}
c_HeadNode3* c_HeadNode3::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node5::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode3::mark(){
	c_Node5::mark();
}
String c_HeadNode3::debug(){
	String t="(HeadNode)\n";
	t=c_Node5::debug()+t;
	return t;
}
int bb_input_MouseDown(int t_button){
	DBG_ENTER("MouseDown")
	DBG_LOCAL(t_button,"button")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/input.monkey<66>");
	int t_=((bb_input_device->p_KeyDown(1+t_button))?1:0);
	return t_;
}
int bb_event_MOUSE_STATE_NONE;
int bb_event_MOUSE_STATE_RIGHT;
int bb_event_MOUSE_STATE_BOTH_LEFT;
int bb_event_MOUSE_STATE_BOTH_RIGHT;
int bb_event_MOUSE_STATE_LEFT;
c_BackwardsList::c_BackwardsList(){
	m__list=0;
}
c_BackwardsList* c_BackwardsList::m_new(c_List2* t_list){
	DBG_ENTER("BackwardsList.new")
	c_BackwardsList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<353>");
	gc_assign(m__list,t_list);
	return this;
}
c_BackwardsList* c_BackwardsList::m_new2(){
	DBG_ENTER("BackwardsList.new")
	c_BackwardsList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<350>");
	return this;
}
c_BackwardsEnumerator* c_BackwardsList::p_ObjectEnumerator(){
	DBG_ENTER("BackwardsList.ObjectEnumerator")
	c_BackwardsList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<357>");
	c_BackwardsEnumerator* t_=(new c_BackwardsEnumerator)->m_new(m__list);
	return t_;
}
void c_BackwardsList::mark(){
	Object::mark();
	gc_mark_q(m__list);
}
String c_BackwardsList::debug(){
	String t="(BackwardsList)\n";
	t+=dbg_decl("_list",&m__list);
	return t;
}
c_BackwardsEnumerator::c_BackwardsEnumerator(){
	m__list=0;
	m__curr=0;
}
c_BackwardsEnumerator* c_BackwardsEnumerator::m_new(c_List2* t_list){
	DBG_ENTER("BackwardsEnumerator.new")
	c_BackwardsEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<369>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<370>");
	gc_assign(m__curr,t_list->m__head->m__pred);
	return this;
}
c_BackwardsEnumerator* c_BackwardsEnumerator::m_new2(){
	DBG_ENTER("BackwardsEnumerator.new")
	c_BackwardsEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<366>");
	return this;
}
bool c_BackwardsEnumerator::p_HasNext(){
	DBG_ENTER("BackwardsEnumerator.HasNext")
	c_BackwardsEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<374>");
	while(m__curr->m__pred->m__succ!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<375>");
		gc_assign(m__curr,m__curr->m__pred);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<377>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Gadget* c_BackwardsEnumerator::p_NextObject(){
	DBG_ENTER("BackwardsEnumerator.NextObject")
	c_BackwardsEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<381>");
	c_Gadget* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<382>");
	gc_assign(m__curr,m__curr->m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<383>");
	return t_data;
}
void c_BackwardsEnumerator::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_BackwardsEnumerator::debug(){
	String t="(BackwardsEnumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
int bb_gui_RectangleContainsPoint(int t_rx,int t_ry,int t_rw,int t_rh,int t_x,int t_y){
	DBG_ENTER("RectangleContainsPoint")
	DBG_LOCAL(t_rx,"rx")
	DBG_LOCAL(t_ry,"ry")
	DBG_LOCAL(t_rw,"rw")
	DBG_LOCAL(t_rh,"rh")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<19>");
	if(t_x<t_rx || t_x>=t_rx+t_rw || t_y<t_ry || t_y>=t_ry+t_rh){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<20>");
		return 0;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<23>");
	return 1;
}
c_Project::c_Project(){
	m_patch=0;
	m_boxSelected=0;
	m_viewPanel=0;
	m_panel=0;
	m_tray=0;
	m_path=String();
}
void c_Project::p_Update(){
	DBG_ENTER("Project.Update")
	c_Project *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<46>");
	c_Enumerator3* t_=m_patch->m_sparks->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Spark* t_spark=t_->p_NextObject();
		DBG_LOCAL(t_spark,"spark")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<47>");
		t_spark->p_Update();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<50>");
	if((bb_input_KeyHit(13))!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<51>");
		if(m_boxSelected!=0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<52>");
			m_boxSelected->p_Execute();
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<56>");
	if(m_boxSelected!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<57>");
		bb_functions_Copy(m_viewPanel->m_box,m_boxSelected);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<59>");
		bb_functions_Fill(m_viewPanel->m_box,0);
	}
}
c_Project* c_Project::m_new(String t_path){
	DBG_ENTER("Project.new")
	c_Project *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<19>");
	c_ContainerGadget::m_new2();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<20>");
	m_x=0;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<21>");
	m_y=22;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<23>");
	m_w=640;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<24>");
	m_h=480;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<26>");
	bb_main_APP->m_window->p_AddChild(this);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<27>");
	gc_assign(m_tray,(new c_Tray)->m_new(0,0,538,30));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<28>");
	gc_assign(m_patch,(new c_Patch)->m_new(0,31,538,425));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<30>");
	gc_assign(m_panel,(new c_Panel)->m_new(539,0,100,375));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<31>");
	gc_assign(m_viewPanel,(new c_View)->m_new(m_panel->m_x,m_panel->m_y+m_panel->m_h+1,m_panel->m_w,80));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<33>");
	p_AddChild(m_patch);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<34>");
	p_AddChild(m_tray);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<35>");
	p_AddChild(m_panel);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<36>");
	p_AddChild(m_viewPanel);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<38>");
	this->m_path=t_path;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<40>");
	if(t_path!=String()){
		DBG_BLOCK();
	}
	return this;
}
c_Project* c_Project::m_new2(){
	DBG_ENTER("Project.new")
	c_Project *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/project.monkey<9>");
	c_ContainerGadget::m_new2();
	return this;
}
void c_Project::mark(){
	c_ContainerGadget::mark();
	gc_mark_q(m_patch);
	gc_mark_q(m_boxSelected);
	gc_mark_q(m_viewPanel);
	gc_mark_q(m_panel);
	gc_mark_q(m_tray);
}
String c_Project::debug(){
	String t="(Project)\n";
	t=c_ContainerGadget::debug()+t;
	t+=dbg_decl("path",&m_path);
	t+=dbg_decl("boxSelected",&m_boxSelected);
	t+=dbg_decl("tray",&m_tray);
	t+=dbg_decl("patch",&m_patch);
	t+=dbg_decl("panel",&m_panel);
	t+=dbg_decl("viewPanel",&m_viewPanel);
	return t;
}
c_Patch::c_Patch(){
	m_sparks=(new c_List4)->m_new();
	m_wires=(new c_List5)->m_new();
	m_boxes=(new c_List6)->m_new();
	m_boxOver=0;
	m_inOver=0;
	m_outOver=0;
}
void c_Patch::p_UpdateHover(c_Event* t_event,int t_generous){
	DBG_ENTER("Patch.UpdateHover")
	c_Patch *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_generous,"generous")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<100>");
	m_boxOver=0;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<101>");
	m_inOver=-1;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<102>");
	m_outOver=0;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<104>");
	c_Enumerator5* t_=m_boxes->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Box* t_box=t_->p_NextObject();
		DBG_LOCAL(t_box,"box")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<105>");
		if((bb_gui_RectangleContainsPoint(t_box->m_x,t_box->m_y,t_box->m_w,t_box->m_h,t_event->m_x,t_event->m_y))!=0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<106>");
			gc_assign(m_boxOver,t_box);
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<110>");
	if(m_boxOver!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<111>");
		for(int t_n=0;t_n<m_boxOver->m_ins;t_n=t_n+1){
			DBG_BLOCK();
			DBG_LOCAL(t_n,"n")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<112>");
			if((t_generous)!=0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<113>");
				if(((bb_gui_RectangleContainsPoint(int(Float(m_boxOver->m_x)+(Float(t_n)-FLOAT(0.5))*Float(m_boxOver->m_gap)),m_boxOver->m_y,m_boxOver->m_gap,m_boxOver->m_h,t_event->m_x,t_event->m_y))!=0) || m_boxOver->m_ins==1){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<114>");
					m_inOver=t_n;
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<117>");
				if((bb_gui_RectangleContainsPoint(m_boxOver->m_x+t_n*m_boxOver->m_gap,m_boxOver->m_y,8,3,t_event->m_x,t_event->m_y))!=0){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<118>");
					m_inOver=t_n;
				}
			}
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<123>");
		for(int t_n2=0;t_n2<m_boxOver->m_outs;t_n2=t_n2+1){
			DBG_BLOCK();
			DBG_LOCAL(t_n2,"n")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<124>");
			if((bb_gui_RectangleContainsPoint(m_boxOver->m_x+t_n2*m_boxOver->m_gap,m_boxOver->m_y+m_boxOver->m_h-3,8,3,t_event->m_x,t_event->m_y))!=0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<125>");
				m_outOver=1;
			}
		}
	}
}
c_Gadget* c_Patch::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("Patch.HandleEvent")
	c_Patch *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<21>");
	int t_1=t_event->m_id;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<22>");
	if(t_1==0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<23>");
		p_UpdateHover(t_event,0);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<24>");
		if(t_1==3){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<25>");
			int t_generous=0;
			DBG_LOCAL(t_generous,"generous")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<27>");
			if(bb_main__dragMode==2){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<28>");
				t_generous=1;
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<31>");
			p_UpdateHover(t_event,t_generous);
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<33>");
			int t_2=bb_main__dragMode;
			DBG_LOCAL(t_2,"2")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<34>");
			if(t_2==1){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<35>");
				bb_main_APP->p_project()->m_boxSelected->m_x=bb_main_sx+t_event->m_dx;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<36>");
				bb_main_APP->p_project()->m_boxSelected->m_y=bb_main_sy+t_event->m_dy;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<37>");
				if(t_2==2){
					DBG_BLOCK();
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<40>");
			if(t_1==4){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<41>");
				m_ox=bb_main_sx+t_event->m_dx;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<42>");
				m_oy=bb_main_sy+t_event->m_dy;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<43>");
				if(t_1==1){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<44>");
					bb_main__dragMode=0;
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<46>");
					if(m_boxOver!=0){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<47>");
						if(m_outOver==1){
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<48>");
							bb_main__dragMode=2;
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<49>");
							gc_assign(bb_main_from,m_boxOver);
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<51>");
							if(m_inOver!=-1){
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<52>");
								c_Enumerator4* t_=m_wires->p_ObjectEnumerator();
								while(t_->p_HasNext()){
									DBG_BLOCK();
									c_Wire* t_wire=t_->p_NextObject();
									DBG_LOCAL(t_wire,"wire")
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<53>");
									if(t_wire->m_b==m_boxOver && t_wire->m_bId==m_inOver){
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<54>");
										bb_main_DeleteWire(t_wire);
									}
								}
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<58>");
								bb_main__dragMode=1;
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<60>");
								bb_patch_SelectBox(m_boxOver);
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<61>");
								m_boxes->p_Remove5(bb_main_APP->p_project()->m_boxSelected);
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<62>");
								m_boxes->p_AddLast6(bb_main_APP->p_project()->m_boxSelected);
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<63>");
								bb_main_sx=bb_main_APP->p_project()->m_boxSelected->m_x;
								bb_main_sy=bb_main_APP->p_project()->m_boxSelected->m_y;
							}
						}
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<66>");
						bb_patch_SelectBox(0);
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<68>");
					if(t_1==2){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<69>");
						bb_main_sx=m_ox;
						bb_main_sy=m_oy;
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<70>");
						if(t_1==5){
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<71>");
							if(bb_main__dragMode==2){
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<72>");
								if(m_boxOver!=0){
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<73>");
									if(m_inOver!=-1){
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<74>");
										if(!bb_main_CycleCheck(m_boxOver,bb_main_from)){
											DBG_BLOCK();
											DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<75>");
											m_wires->p_AddLast5((new c_Wire)->m_new(bb_main_from,m_boxOver,m_inOver));
										}
									}
								}
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<79>");
								if(bb_main__dragMode==1){
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<80>");
									c_Gadget* t_gadget=m_window->p_HandleEvent((new c_Event)->m_new(-1,c_Event::m_globalWindow));
									DBG_LOCAL(t_gadget,"gadget")
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<82>");
									if(t_gadget!=(bb_main_APP->p_patch())){
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<83>");
										bb_main_DeleteBox(bb_main_APP->p_project()->m_boxSelected);
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<84>");
										bb_patch_SelectBox(0);
									}
								}
							}
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<87>");
							bb_main__dragMode=0;
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<88>");
							if(t_1==6){
								DBG_BLOCK();
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<90>");
								if(t_1==7){
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<91>");
									if(bb_main_APP->p_project()->m_boxSelected!=0 && bb_main_APP->p_project()->m_boxSelected==m_boxOver && bb_main_APP->p_project()->m_boxSelected->m_ins==0){
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<92>");
										bb_main_APP->p_project()->m_boxSelected->p_Execute();
									}
								}
							}
						}
					}
				}
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<96>");
	c_Gadget* t_3=(this);
	return t_3;
}
void c_Patch::p_OnRender(){
	DBG_ENTER("Patch.OnRender")
	c_Patch *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<132>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<133>");
	bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<134>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<135>");
	bb_graphics_DrawRect(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
}
void c_Patch::p_OnRenderInterior(){
	DBG_ENTER("Patch.OnRenderInterior")
	c_Patch *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<139>");
	c_Enumerator5* t_=m_boxes->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Box* t_box=t_->p_NextObject();
		DBG_LOCAL(t_box,"box")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<140>");
		t_box->p_Render();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<143>");
	c_Enumerator4* t_2=m_wires->p_ObjectEnumerator();
	while(t_2->p_HasNext()){
		DBG_BLOCK();
		c_Wire* t_wire=t_2->p_NextObject();
		DBG_LOCAL(t_wire,"wire")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<144>");
		t_wire->p_Render();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<147>");
	c_Enumerator3* t_3=m_sparks->p_ObjectEnumerator();
	while(t_3->p_HasNext()){
		DBG_BLOCK();
		c_Spark* t_spark=t_3->p_NextObject();
		DBG_LOCAL(t_spark,"spark")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<148>");
		t_spark->p_Render();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<151>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<155>");
	int t_32=bb_main__dragMode;
	DBG_LOCAL(t_32,"3")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<156>");
	if(t_32==0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<157>");
		if((m_outOver)!=0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<160>");
			bb_graphics_DrawImage(bb_main_imgO,Float(p_GetLocalX(m_window->m__mouseX,(m_window))),Float(p_GetLocalY(m_window->m__mouseY,(m_window))),0);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<161>");
			if(m_inOver!=-1){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<162>");
				int t_yes=0;
				DBG_LOCAL(t_yes,"yes")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<164>");
				c_Enumerator4* t_4=m_wires->p_ObjectEnumerator();
				while(t_4->p_HasNext()){
					DBG_BLOCK();
					c_Wire* t_wire2=t_4->p_NextObject();
					DBG_LOCAL(t_wire2,"wire")
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<165>");
					if(t_wire2->m_b==m_boxOver && t_wire2->m_bId==m_inOver){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<166>");
						t_yes=1;
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<167>");
						break;
					}
				}
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<171>");
				if((t_yes)!=0){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<173>");
					bb_graphics_DrawImage(bb_main_imgX,Float(p_GetLocalX(m_window->m__mouseX,(m_window))),Float(p_GetLocalY(m_window->m__mouseY,(m_window))),0);
				}
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<176>");
		if(t_32==2){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<177>");
			if(m_boxOver==0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<178>");
				c_Wire::m_DrawFrom(bb_main_from,p_GetLocalX(m_window->m__mouseX,(m_window)),p_GetLocalY(m_window->m__mouseY,(m_window)));
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<180>");
				c_Wire::m_DrawFromTo(bb_main_from,m_boxOver,m_inOver);
			}
		}
	}
}
c_Patch* c_Patch::m_new(int t_x,int t_y,int t_w,int t_h){
	DBG_ENTER("Patch.new")
	c_Patch *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<16>");
	c_ViewGadget::m_new();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<17>");
	this->m_x=t_x;
	this->m_y=t_y;
	this->m_w=t_w;
	this->m_h=t_h;
	return this;
}
c_Patch* c_Patch::m_new2(){
	DBG_ENTER("Patch.new")
	c_Patch *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<9>");
	c_ViewGadget::m_new();
	return this;
}
void c_Patch::mark(){
	c_ViewGadget::mark();
	gc_mark_q(m_sparks);
	gc_mark_q(m_wires);
	gc_mark_q(m_boxes);
	gc_mark_q(m_boxOver);
}
String c_Patch::debug(){
	String t="(Patch)\n";
	t=c_ViewGadget::debug()+t;
	t+=dbg_decl("boxOver",&m_boxOver);
	t+=dbg_decl("inOver",&m_inOver);
	t+=dbg_decl("outOver",&m_outOver);
	t+=dbg_decl("boxes",&m_boxes);
	t+=dbg_decl("wires",&m_wires);
	t+=dbg_decl("sparks",&m_sparks);
	return t;
}
c_Spark::c_Spark(){
	m_arrived=0;
	m_n=0;
	m_wire=0;
}
c_Spark* c_Spark::m_new(c_Wire* t_wire){
	DBG_ENTER("Spark.new")
	c_Spark *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_wire,"wire")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<15>");
	gc_assign(this->m_wire,t_wire);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<16>");
	t_wire->m_b->m_done=false;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<18>");
	c_Enumerator3* t_=bb_main_APP->p_patch()->m_sparks->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Spark* t_other=t_->p_NextObject();
		DBG_LOCAL(t_other,"other")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<19>");
		if(t_other->m_wire==t_wire){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<20>");
			bb_main_APP->p_patch()->m_sparks->p_Remove3(t_other);
		}
	}
	return this;
}
c_Spark* c_Spark::m_new2(){
	DBG_ENTER("Spark.new")
	c_Spark *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<9>");
	return this;
}
void c_Spark::p_Update(){
	DBG_ENTER("Spark.Update")
	c_Spark *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<26>");
	if(!((m_arrived)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<27>");
		m_n=m_n+6;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<28>");
		if(m_n>=30){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<29>");
			m_n=30;
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<30>");
			m_arrived=1;
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<31>");
			Array<bool > t_satisfied=Array<bool >(m_wire->m_b->m_ins);
			DBG_LOCAL(t_satisfied,"satisfied")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<32>");
			t_satisfied.At(m_wire->m_bId)=true;
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<34>");
			for(int t_n=0;t_n<m_wire->m_b->m_ins;t_n=t_n+1){
				DBG_BLOCK();
				DBG_LOCAL(t_n,"n")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<35>");
				c_Enumerator4* t_=bb_main_APP->p_patch()->m_wires->p_ObjectEnumerator();
				while(t_->p_HasNext()){
					DBG_BLOCK();
					c_Wire* t_other=t_->p_NextObject();
					DBG_LOCAL(t_other,"other")
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<36>");
					if(t_other->m_b==m_wire->m_b && t_other!=m_wire){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<37>");
						t_satisfied.At(t_n)=t_other->m_a->m_done;
					}
				}
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<42>");
			c_Enumerator3* t_2=bb_main_APP->p_patch()->m_sparks->p_ObjectEnumerator();
			while(t_2->p_HasNext()){
				DBG_BLOCK();
				c_Spark* t_spark=t_2->p_NextObject();
				DBG_LOCAL(t_spark,"spark")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<43>");
				if(t_spark->m_wire->m_b==m_wire->m_b){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<44>");
					if(!((t_spark->m_arrived)!=0)){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<45>");
						t_satisfied.At(t_spark->m_wire->m_bId)=false;
					}
				}
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<50>");
			int t_shoot=1;
			DBG_LOCAL(t_shoot,"shoot")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<52>");
			for(int t_n2=0;t_n2<m_wire->m_b->m_ins;t_n2=t_n2+1){
				DBG_BLOCK();
				DBG_LOCAL(t_n2,"n")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<53>");
				if(!t_satisfied.At(t_n2)){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<54>");
					t_shoot=0;
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<55>");
					break;
				}
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<59>");
			if((t_shoot)!=0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<60>");
				c_Enumerator3* t_3=bb_main_APP->p_patch()->m_sparks->p_ObjectEnumerator();
				while(t_3->p_HasNext()){
					DBG_BLOCK();
					c_Spark* t_spark2=t_3->p_NextObject();
					DBG_LOCAL(t_spark2,"spark")
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<61>");
					if(t_spark2->m_wire->m_b==m_wire->m_b){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<62>");
						bb_main_APP->p_patch()->m_sparks->p_Remove3(t_spark2);
					}
				}
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<66>");
				m_wire->m_b->p_Execute();
			}
		}
	}
}
void c_Spark::p_Render(){
	DBG_ENTER("Spark.Render")
	c_Spark *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<73>");
	int t_x0=m_wire->m_a->m_x+3;
	DBG_LOCAL(t_x0,"x0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<74>");
	int t_y0=m_wire->m_a->m_y+m_wire->m_a->m_h-2;
	DBG_LOCAL(t_y0,"y0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<75>");
	int t_x1=m_wire->m_b->m_x+3+m_wire->m_bId*m_wire->m_b->m_gap;
	DBG_LOCAL(t_x1,"x1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<76>");
	int t_y1=m_wire->m_b->m_y+1;
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<77>");
	Float t_x=FLOAT(.0);
	Float t_y=FLOAT(.0);
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<78>");
	Float t_t=Float(m_n)/FLOAT(30.0);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<79>");
	t_x=Float(t_x1)*t_t+Float(t_x0)*(FLOAT(1.0)-t_t);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<80>");
	t_y=Float(t_y1)*t_t+Float(t_y0)*(FLOAT(1.0)-t_t);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<81>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/spark.monkey<82>");
	bb_graphics_DrawOval(t_x-FLOAT(2.0),t_y-FLOAT(2.0),FLOAT(5.0),FLOAT(5.0));
}
void c_Spark::mark(){
	Object::mark();
	gc_mark_q(m_wire);
}
String c_Spark::debug(){
	String t="(Spark)\n";
	t+=dbg_decl("wire",&m_wire);
	t+=dbg_decl("n",&m_n);
	t+=dbg_decl("arrived",&m_arrived);
	return t;
}
c_List4::c_List4(){
	m__head=((new c_HeadNode4)->m_new());
}
c_List4* c_List4::m_new(){
	DBG_ENTER("List.new")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node6* c_List4::p_AddLast4(c_Spark* t_data){
	DBG_ENTER("List.AddLast")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node6* t_=(new c_Node6)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List4* c_List4::m_new2(Array<c_Spark* > t_data){
	DBG_ENTER("List.new")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<c_Spark* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Spark* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast4(t_t);
	}
	return this;
}
c_Enumerator3* c_List4::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<186>");
	c_Enumerator3* t_=(new c_Enumerator3)->m_new(this);
	return t_;
}
bool c_List4::p_Equals3(c_Spark* t_lhs,c_Spark* t_rhs){
	DBG_ENTER("List.Equals")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
int c_List4::p_RemoveEach2(c_Spark* t_value){
	DBG_ENTER("List.RemoveEach")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<151>");
	c_Node6* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<152>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<153>");
		c_Node6* t_succ=t_node->m__succ;
		DBG_LOCAL(t_succ,"succ")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<154>");
		if(p_Equals3(t_node->m__data,t_value)){
			DBG_BLOCK();
			t_node->p_Remove2();
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<155>");
		t_node=t_succ;
	}
	return 0;
}
void c_List4::p_Remove3(c_Spark* t_value){
	DBG_ENTER("List.Remove")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<137>");
	p_RemoveEach2(t_value);
}
int c_List4::p_Clear(){
	DBG_ENTER("List.Clear")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<36>");
	gc_assign(m__head->m__succ,m__head);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<37>");
	gc_assign(m__head->m__pred,m__head);
	return 0;
}
void c_List4::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List4::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node6::c_Node6(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node6* c_Node6::m_new(c_Node6* t_succ,c_Node6* t_pred,c_Spark* t_data){
	DBG_ENTER("Node.new")
	c_Node6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node6* c_Node6::m_new2(){
	DBG_ENTER("Node.new")
	c_Node6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node6::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node6::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node6::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode4::c_HeadNode4(){
}
c_HeadNode4* c_HeadNode4::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node6::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode4::mark(){
	c_Node6::mark();
}
String c_HeadNode4::debug(){
	String t="(HeadNode)\n";
	t=c_Node6::debug()+t;
	return t;
}
c_Enumerator3::c_Enumerator3(){
	m__list=0;
	m__curr=0;
}
c_Enumerator3* c_Enumerator3::m_new(c_List4* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator3* c_Enumerator3::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator3::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Spark* c_Enumerator3::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<338>");
	c_Spark* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator3::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator3::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
c_Wire::c_Wire(){
	m_b=0;
	m_bId=0;
	m_a=0;
}
c_Wire* c_Wire::m_new(c_Box* t_a,c_Box* t_b,int t_bId){
	DBG_ENTER("Wire.new")
	c_Wire *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_a,"a")
	DBG_LOCAL(t_b,"b")
	DBG_LOCAL(t_bId,"bId")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<14>");
	gc_assign(this->m_a,t_a);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<15>");
	gc_assign(this->m_b,t_b);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<16>");
	this->m_bId=t_bId;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<18>");
	c_Enumerator4* t_=bb_main_APP->p_patch()->m_wires->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Wire* t_other=t_->p_NextObject();
		DBG_LOCAL(t_other,"other")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<19>");
		if(t_other->m_b==t_b && t_other->m_bId==t_bId){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<20>");
			bb_main_APP->p_patch()->m_wires->p_Remove4(t_other);
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<24>");
	if(t_a->m_done){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<25>");
		bb_main_APP->p_patch()->m_sparks->p_AddLast4((new c_Spark)->m_new(this));
	}
	return this;
}
c_Wire* c_Wire::m_new2(){
	DBG_ENTER("Wire.new")
	c_Wire *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<9>");
	return this;
}
void c_Wire::p_Render(){
	DBG_ENTER("Wire.Render")
	c_Wire *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<30>");
	bb_graphics_SetColor(FLOAT(238.0),FLOAT(221.0),FLOAT(238.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<32>");
	if(m_a->m_done){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<33>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(0.0));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<36>");
	if(bb_main__dragMode==2){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<37>");
		if(bb_main_APP->p_patch()->m_boxOver!=0 && bb_main_APP->p_patch()->m_boxOver==m_b && bb_main_APP->p_patch()->m_inOver==m_bId){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<38>");
			if(!bb_main_CycleCheck(bb_main_APP->p_patch()->m_boxOver,bb_main_from)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<39>");
				bb_graphics_SetColor(FLOAT(255.0),FLOAT(0.0),FLOAT(0.0));
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<42>");
		if(bb_main__dragMode==0 && bb_main_APP->p_patch()->m_boxOver==m_b && bb_main_APP->p_patch()->m_inOver==m_bId){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<43>");
			bb_graphics_SetColor(FLOAT(255.0),FLOAT(0.0),FLOAT(0.0));
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<46>");
	int t_x0=m_a->m_x+3;
	DBG_LOCAL(t_x0,"x0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<47>");
	int t_y0=m_a->m_y+m_a->m_h-2;
	DBG_LOCAL(t_y0,"y0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<48>");
	int t_x1=m_b->m_x+3+m_bId*m_b->m_gap;
	DBG_LOCAL(t_x1,"x1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<49>");
	int t_y1=m_b->m_y+1;
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<50>");
	bb_graphics_DrawLine(Float(t_x0),Float(t_y0),Float(t_x1),Float(t_y1));
}
void c_Wire::m_DrawFrom(c_Box* t_from,int t_x1,int t_y1){
	DBG_ENTER("Wire.DrawFrom")
	DBG_LOCAL(t_from,"from")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<54>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<55>");
	int t_x0=t_from->m_x+3;
	DBG_LOCAL(t_x0,"x0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<56>");
	int t_y0=t_from->m_y+t_from->m_h-2;
	DBG_LOCAL(t_y0,"y0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<57>");
	bb_graphics_DrawLine(Float(t_x0),Float(t_y0),Float(t_x1),Float(t_y1));
}
void c_Wire::m_DrawFromTo(c_Box* t_a,c_Box* t_b,int t_bId){
	DBG_ENTER("Wire.DrawFromTo")
	DBG_LOCAL(t_a,"a")
	DBG_LOCAL(t_b,"b")
	DBG_LOCAL(t_bId,"bId")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<61>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<63>");
	if(bb_main_CycleCheck(t_b,t_a)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<64>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(0.0),FLOAT(0.0));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<67>");
	int t_x0=t_a->m_x+3;
	DBG_LOCAL(t_x0,"x0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<68>");
	int t_y0=t_a->m_y+t_a->m_h-2;
	DBG_LOCAL(t_y0,"y0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<69>");
	int t_x1=t_b->m_x+3+t_bId*t_b->m_gap;
	DBG_LOCAL(t_x1,"x1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<70>");
	int t_y1=t_b->m_y+1;
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/wire.monkey<71>");
	bb_graphics_DrawLine(Float(t_x0),Float(t_y0),Float(t_x1),Float(t_y1));
}
void c_Wire::mark(){
	Object::mark();
	gc_mark_q(m_b);
	gc_mark_q(m_a);
}
String c_Wire::debug(){
	String t="(Wire)\n";
	t+=dbg_decl("a",&m_a);
	t+=dbg_decl("b",&m_b);
	t+=dbg_decl("bId",&m_bId);
	return t;
}
c_Box::c_Box(){
	m_ins=2;
	m_done=false;
	m_kind=String();
	m_state=Array<Array<int > >();
	m_settings=(new c_StringMap)->m_new();
	m_x=0;
	m_y=0;
	m_w=0;
	m_h=22;
	m_gap=16;
	m_outs=1;
	m_id=0;
}
void c_Box::p_Execute(){
	DBG_ENTER("Box.Execute")
	c_Box *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<47>");
	Array<c_Box* > t_in=Array<c_Box* >(m_ins);
	DBG_LOCAL(t_in,"in")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<49>");
	c_Enumerator4* t_=bb_main_APP->p_patch()->m_wires->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Wire* t_wire=t_->p_NextObject();
		DBG_LOCAL(t_wire,"wire")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<50>");
		if(t_wire->m_b==this){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<51>");
			gc_assign(t_in.At(t_wire->m_bId),t_wire->m_a);
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<55>");
	bb_functions_ExecuteBox(this,t_in);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<56>");
	bb_main_MakeSparks(this);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<57>");
	m_done=true;
}
bool c_Box::p_isClickable(){
	DBG_ENTER("Box.isClickable")
	c_Box *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<22>");
	bool t_=m_ins==0 && m_kind!=String(L"omni",4) && m_kind!=String(L"in",2);
	return t_;
}
void c_Box::p_Render(){
	DBG_ENTER("Box.Render")
	c_Box *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<61>");
	bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<63>");
	if(bb_main_APP->p_project()->m_boxSelected==this){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<64>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<67>");
	bb_graphics_DrawRect(Float(m_x),Float(m_y),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<68>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<69>");
	bb_graphics_DrawRect(Float(m_x+1),Float(m_y+1),Float(m_w-2),Float(m_h-2));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<71>");
	if(p_isClickable()){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<72>");
		bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<73>");
		bb_graphics_DrawRect(Float(m_x+1),Float(m_y+4),Float(m_w-2),Float(m_h-8));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<76>");
	bb_graphics_SetColor(FLOAT(238.0),FLOAT(221.0),FLOAT(238.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<78>");
	for(int t_n=0;t_n<m_ins;t_n=t_n+1){
		DBG_BLOCK();
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<79>");
		bb_graphics_DrawRect(Float(m_x+t_n*m_gap),Float(m_y),FLOAT(9.0),FLOAT(3.0));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<82>");
	for(int t_n2=0;t_n2<m_outs;t_n2=t_n2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_n2,"n")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<83>");
		bb_graphics_DrawRect(Float(m_x+t_n2*16),Float(m_y+m_h-3),FLOAT(9.0),FLOAT(3.0));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<86>");
	if(dynamic_cast<c_ViewBox*>(this)==0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<87>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<89>");
		if(!bb_functions_implementedTemplates->p_Contains3(m_kind)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<90>");
			bb_graphics_SetColor(FLOAT(255.0),FLOAT(0.0),FLOAT(0.0));
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<93>");
		bb_graphics_DrawText(m_kind,Float(m_x+2),Float(m_y+4),FLOAT(0.0),FLOAT(0.0));
	}
}
int c_Box::m_idNext;
c_Box* c_Box::m_new(int t_x,int t_y,c_Template* t_template){
	DBG_ENTER("Box.new")
	c_Box *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_template,"template")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<26>");
	this->m_id=m_idNext;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<27>");
	m_idNext+=1;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<28>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<29>");
	m_kind=t_template->m_name;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<31>");
	c_ValueEnumerator* t_=t_template->m_settings->p_Values()->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Setting* t_setting=t_->p_NextObject();
		DBG_LOCAL(t_setting,"setting")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<32>");
		m_settings->p_Insert2(t_setting->m_name,t_setting->p_Copy());
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<35>");
	m_ins=t_template->m_ins;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<36>");
	m_outs=t_template->m_outs;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<37>");
	m_w=bb_math_Max(8+m_gap*(m_ins-1),int(bb_graphics_TextWidth(m_kind))+4);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<39>");
	if(m_ins>1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<40>");
		m_gap=bb_math_Max(16,(m_w-9*m_ins)/(m_ins-1)+9);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<43>");
	gc_assign(m_state,bb_box_Initialize2dArray(20,15));
	return this;
}
c_Box* c_Box::m_new2(){
	DBG_ENTER("Box.new")
	c_Box *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<9>");
	return this;
}
void c_Box::mark(){
	Object::mark();
	gc_mark_q(m_state);
	gc_mark_q(m_settings);
}
String c_Box::debug(){
	String t="(Box)\n";
	t+=dbg_decl("idNext",&c_Box::m_idNext);
	t+=dbg_decl("id",&m_id);
	t+=dbg_decl("kind",&m_kind);
	t+=dbg_decl("settings",&m_settings);
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("w",&m_w);
	t+=dbg_decl("h",&m_h);
	t+=dbg_decl("gap",&m_gap);
	t+=dbg_decl("ins",&m_ins);
	t+=dbg_decl("outs",&m_outs);
	t+=dbg_decl("state",&m_state);
	t+=dbg_decl("done",&m_done);
	return t;
}
c_List5::c_List5(){
	m__head=((new c_HeadNode5)->m_new());
}
c_List5* c_List5::m_new(){
	DBG_ENTER("List.new")
	c_List5 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node7* c_List5::p_AddLast5(c_Wire* t_data){
	DBG_ENTER("List.AddLast")
	c_List5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node7* t_=(new c_Node7)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List5* c_List5::m_new2(Array<c_Wire* > t_data){
	DBG_ENTER("List.new")
	c_List5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<c_Wire* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Wire* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast5(t_t);
	}
	return this;
}
c_Enumerator4* c_List5::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<186>");
	c_Enumerator4* t_=(new c_Enumerator4)->m_new(this);
	return t_;
}
bool c_List5::p_Equals4(c_Wire* t_lhs,c_Wire* t_rhs){
	DBG_ENTER("List.Equals")
	c_List5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
int c_List5::p_RemoveEach3(c_Wire* t_value){
	DBG_ENTER("List.RemoveEach")
	c_List5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<151>");
	c_Node7* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<152>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<153>");
		c_Node7* t_succ=t_node->m__succ;
		DBG_LOCAL(t_succ,"succ")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<154>");
		if(p_Equals4(t_node->m__data,t_value)){
			DBG_BLOCK();
			t_node->p_Remove2();
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<155>");
		t_node=t_succ;
	}
	return 0;
}
void c_List5::p_Remove4(c_Wire* t_value){
	DBG_ENTER("List.Remove")
	c_List5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<137>");
	p_RemoveEach3(t_value);
}
void c_List5::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List5::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node7::c_Node7(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node7* c_Node7::m_new(c_Node7* t_succ,c_Node7* t_pred,c_Wire* t_data){
	DBG_ENTER("Node.new")
	c_Node7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node7* c_Node7::m_new2(){
	DBG_ENTER("Node.new")
	c_Node7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node7::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node7::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node7::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode5::c_HeadNode5(){
}
c_HeadNode5* c_HeadNode5::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node7::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode5::mark(){
	c_Node7::mark();
}
String c_HeadNode5::debug(){
	String t="(HeadNode)\n";
	t=c_Node7::debug()+t;
	return t;
}
c_Enumerator4::c_Enumerator4(){
	m__list=0;
	m__curr=0;
}
c_Enumerator4* c_Enumerator4::m_new(c_List5* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator4* c_Enumerator4::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator4::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Wire* c_Enumerator4::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<338>");
	c_Wire* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator4::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator4::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
c_List6::c_List6(){
	m__head=((new c_HeadNode6)->m_new());
}
c_List6* c_List6::m_new(){
	DBG_ENTER("List.new")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node8* c_List6::p_AddLast6(c_Box* t_data){
	DBG_ENTER("List.AddLast")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node8* t_=(new c_Node8)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List6* c_List6::m_new2(Array<c_Box* > t_data){
	DBG_ENTER("List.new")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<c_Box* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Box* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast6(t_t);
	}
	return this;
}
c_Enumerator5* c_List6::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<186>");
	c_Enumerator5* t_=(new c_Enumerator5)->m_new(this);
	return t_;
}
bool c_List6::p_Equals5(c_Box* t_lhs,c_Box* t_rhs){
	DBG_ENTER("List.Equals")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
int c_List6::p_RemoveEach4(c_Box* t_value){
	DBG_ENTER("List.RemoveEach")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<151>");
	c_Node8* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<152>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<153>");
		c_Node8* t_succ=t_node->m__succ;
		DBG_LOCAL(t_succ,"succ")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<154>");
		if(p_Equals5(t_node->m__data,t_value)){
			DBG_BLOCK();
			t_node->p_Remove2();
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<155>");
		t_node=t_succ;
	}
	return 0;
}
void c_List6::p_Remove5(c_Box* t_value){
	DBG_ENTER("List.Remove")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<137>");
	p_RemoveEach4(t_value);
}
int c_List6::p_Count(){
	DBG_ENTER("List.Count")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<41>");
	int t_n=0;
	c_Node8* t_node=m__head->m__succ;
	DBG_LOCAL(t_n,"n")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<42>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<43>");
		t_node=t_node->m__succ;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<44>");
		t_n+=1;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<46>");
	return t_n;
}
bool c_List6::p_Contains2(c_Box* t_value){
	DBG_ENTER("List.Contains")
	c_List6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<54>");
	c_Node8* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<55>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<56>");
		if(p_Equals5(t_node->m__data,t_value)){
			DBG_BLOCK();
			return true;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<57>");
		t_node=t_node->m__succ;
	}
	return false;
}
void c_List6::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List6::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node8::c_Node8(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node8* c_Node8::m_new(c_Node8* t_succ,c_Node8* t_pred,c_Box* t_data){
	DBG_ENTER("Node.new")
	c_Node8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node8* c_Node8::m_new2(){
	DBG_ENTER("Node.new")
	c_Node8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node8::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node8::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node8::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode6::c_HeadNode6(){
}
c_HeadNode6* c_HeadNode6::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node8::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode6::mark(){
	c_Node8::mark();
}
String c_HeadNode6::debug(){
	String t="(HeadNode)\n";
	t=c_Node8::debug()+t;
	return t;
}
c_Enumerator5::c_Enumerator5(){
	m__list=0;
	m__curr=0;
}
c_Enumerator5* c_Enumerator5::m_new(c_List6* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator5* c_Enumerator5::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator5::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Box* c_Enumerator5::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<338>");
	c_Box* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator5::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator5::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
void bb_functions_Fill(c_Box* t_box,int t_value){
	DBG_ENTER("Fill")
	DBG_LOCAL(t_box,"box")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<63>");
	for(int t_x=0;t_x<20;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<64>");
		for(int t_y=0;t_y<15;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<65>");
			t_box->m_state.At(t_x).At(t_y)=t_value;
		}
	}
}
void bb_functions_Clear(){
	DBG_ENTER("Clear")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<52>");
	bb_main_APP->p_patch()->m_sparks->p_Clear();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<54>");
	c_Enumerator5* t_=bb_main_APP->p_patch()->m_boxes->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Box* t__box=t_->p_NextObject();
		DBG_LOCAL(t__box,"_box")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<55>");
		bb_functions_Fill(t__box,0);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<56>");
		t__box->m_done=false;
	}
}
int bb_random_Seed;
Float bb_random_Rnd(){
	DBG_ENTER("Rnd")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/random.monkey<21>");
	bb_random_Seed=bb_random_Seed*1664525+1013904223|0;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/random.monkey<22>");
	Float t_=Float(bb_random_Seed>>8&16777215)/FLOAT(16777216.0);
	return t_;
}
Float bb_random_Rnd2(Float t_low,Float t_high){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_low,"low")
	DBG_LOCAL(t_high,"high")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/random.monkey<30>");
	Float t_=bb_random_Rnd3(t_high-t_low)+t_low;
	return t_;
}
Float bb_random_Rnd3(Float t_range){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_range,"range")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/random.monkey<26>");
	Float t_=bb_random_Rnd()*t_range;
	return t_;
}
void bb_functions_Noise(c_Box* t_box){
	DBG_ENTER("Noise")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<73>");
	int t_density=t_box->m_settings->p_Get(String(L"density",7))->m_value*5+5;
	DBG_LOCAL(t_density,"density")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<75>");
	for(int t_x=0;t_x<20;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<76>");
		for(int t_y=0;t_y<15;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<77>");
			t_box->m_state.At(t_x).At(t_y)=((bb_random_Rnd2(FLOAT(0.0),FLOAT(101.0))<Float(t_density))?1:0);
		}
	}
}
c_Rules::c_Rules(){
	m_value=Array<int >();
	m_id=0;
}
c_Rules* c_Rules::m_new(int t_id,Array<int > t_value){
	DBG_ENTER("Rules.new")
	c_Rules *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_id,"id")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<287>");
	this->m_id=t_id;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<288>");
	gc_assign(this->m_value,t_value);
	return this;
}
c_Rules* c_Rules::m_new2(){
	DBG_ENTER("Rules.new")
	c_Rules *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<282>");
	return this;
}
void c_Rules::mark(){
	Object::mark();
	gc_mark_q(m_value);
}
String c_Rules::debug(){
	String t="(Rules)\n";
	t+=dbg_decl("id",&m_id);
	t+=dbg_decl("value",&m_value);
	return t;
}
c_Map3::c_Map3(){
	m_root=0;
}
c_Map3* c_Map3::m_new(){
	DBG_ENTER("Map.new")
	c_Map3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<7>");
	return this;
}
c_Node9* c_Map3::p_FindNode(int t_key){
	DBG_ENTER("Map.FindNode")
	c_Map3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<157>");
	c_Node9* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<159>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<160>");
		int t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_LOCAL(t_cmp,"cmp")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<161>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<162>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<163>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<164>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<166>");
				return t_node;
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<169>");
	return t_node;
}
c_Rules* c_Map3::p_Get2(int t_key){
	DBG_ENTER("Map.Get")
	c_Map3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<101>");
	c_Node9* t_node=p_FindNode(t_key);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<102>");
	if((t_node)!=0){
		DBG_BLOCK();
		return t_node->m_value;
	}
	return 0;
}
int c_Map3::p_RotateLeft3(c_Node9* t_node){
	DBG_ENTER("Map.RotateLeft")
	c_Map3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<251>");
	c_Node9* t_child=t_node->m_right;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<252>");
	gc_assign(t_node->m_right,t_child->m_left);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<253>");
	if((t_child->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<254>");
		gc_assign(t_child->m_left->m_parent,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<256>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<257>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<258>");
		if(t_node==t_node->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<259>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<261>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<264>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<266>");
	gc_assign(t_child->m_left,t_node);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<267>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map3::p_RotateRight3(c_Node9* t_node){
	DBG_ENTER("Map.RotateRight")
	c_Map3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<271>");
	c_Node9* t_child=t_node->m_left;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<272>");
	gc_assign(t_node->m_left,t_child->m_right);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<273>");
	if((t_child->m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<274>");
		gc_assign(t_child->m_right->m_parent,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<276>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<277>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<278>");
		if(t_node==t_node->m_parent->m_right){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<279>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<281>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<284>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<286>");
	gc_assign(t_child->m_right,t_node);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<287>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map3::p_InsertFixup3(c_Node9* t_node){
	DBG_ENTER("Map.InsertFixup")
	c_Map3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<212>");
	while(((t_node->m_parent)!=0) && t_node->m_parent->m_color==-1 && ((t_node->m_parent->m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<213>");
		if(t_node->m_parent==t_node->m_parent->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<214>");
			c_Node9* t_uncle=t_node->m_parent->m_parent->m_right;
			DBG_LOCAL(t_uncle,"uncle")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<215>");
			if(((t_uncle)!=0) && t_uncle->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<216>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<217>");
				t_uncle->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<218>");
				t_uncle->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<219>");
				t_node=t_uncle->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<221>");
				if(t_node==t_node->m_parent->m_right){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<222>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<223>");
					p_RotateLeft3(t_node);
				}
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<225>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<226>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<227>");
				p_RotateRight3(t_node->m_parent->m_parent);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<230>");
			c_Node9* t_uncle2=t_node->m_parent->m_parent->m_left;
			DBG_LOCAL(t_uncle2,"uncle")
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<231>");
			if(((t_uncle2)!=0) && t_uncle2->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<232>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<233>");
				t_uncle2->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<234>");
				t_uncle2->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<235>");
				t_node=t_uncle2->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<237>");
				if(t_node==t_node->m_parent->m_left){
					DBG_BLOCK();
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<238>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<239>");
					p_RotateRight3(t_node);
				}
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<241>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<242>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<243>");
				p_RotateLeft3(t_node->m_parent->m_parent);
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<247>");
	m_root->m_color=1;
	return 0;
}
bool c_Map3::p_Set3(int t_key,c_Rules* t_value){
	DBG_ENTER("Map.Set")
	c_Map3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<29>");
	c_Node9* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<30>");
	c_Node9* t_parent=0;
	int t_cmp=0;
	DBG_LOCAL(t_parent,"parent")
	DBG_LOCAL(t_cmp,"cmp")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<32>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<33>");
		t_parent=t_node;
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<34>");
		t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<35>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<36>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<37>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<38>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<40>");
				gc_assign(t_node->m_value,t_value);
				DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<41>");
				return false;
			}
		}
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<45>");
	t_node=(new c_Node9)->m_new(t_key,t_value,-1,t_parent);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<47>");
	if((t_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<48>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<49>");
			gc_assign(t_parent->m_right,t_node);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<51>");
			gc_assign(t_parent->m_left,t_node);
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<53>");
		p_InsertFixup3(t_node);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<55>");
		gc_assign(m_root,t_node);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<57>");
	return true;
}
bool c_Map3::p_Insert3(int t_key,c_Rules* t_value){
	DBG_ENTER("Map.Insert")
	c_Map3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<146>");
	bool t_=p_Set3(t_key,t_value);
	return t_;
}
void c_Map3::mark(){
	Object::mark();
	gc_mark_q(m_root);
}
String c_Map3::debug(){
	String t="(Map)\n";
	t+=dbg_decl("root",&m_root);
	return t;
}
c_IntMap2::c_IntMap2(){
}
c_IntMap2* c_IntMap2::m_new(){
	DBG_ENTER("IntMap.new")
	c_IntMap2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<534>");
	c_Map3::m_new();
	return this;
}
int c_IntMap2::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("IntMap.Compare")
	c_IntMap2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<537>");
	int t_=t_lhs-t_rhs;
	return t_;
}
void c_IntMap2::mark(){
	c_Map3::mark();
}
String c_IntMap2::debug(){
	String t="(IntMap)\n";
	t=c_Map3::debug()+t;
	return t;
}
c_IntMap2* bb_gadgets_ruleTables;
c_Node9::c_Node9(){
	m_key=0;
	m_right=0;
	m_left=0;
	m_value=0;
	m_color=0;
	m_parent=0;
}
c_Node9* c_Node9::m_new(int t_key,c_Rules* t_value,int t_color,c_Node9* t_parent){
	DBG_ENTER("Node.new")
	c_Node9 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_color,"color")
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<364>");
	this->m_key=t_key;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<365>");
	gc_assign(this->m_value,t_value);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<366>");
	this->m_color=t_color;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<367>");
	gc_assign(this->m_parent,t_parent);
	return this;
}
c_Node9* c_Node9::m_new2(){
	DBG_ENTER("Node.new")
	c_Node9 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<361>");
	return this;
}
void c_Node9::mark(){
	Object::mark();
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_value);
	gc_mark_q(m_parent);
}
String c_Node9::debug(){
	String t="(Node)\n";
	t+=dbg_decl("key",&m_key);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("color",&m_color);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
Array<Array<int > > bb_box_Initialize2dArray(int t_width,int t_height){
	DBG_ENTER("Initialize2dArray")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<127>");
	Array<Array<int > > t_a=Array<Array<int > >();
	DBG_LOCAL(t_a,"a")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<129>");
	t_a=t_a.Resize(t_width);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<131>");
	for(int t_x=0;t_x<t_width;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<132>");
		gc_assign(t_a.At(t_x),t_a.At(t_x).Resize(t_height));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<135>");
	return t_a;
}
void bb_functions_Automata9Sum8(c_Box* t_out,c_Box* t_in,Array<int > t_rules){
	DBG_ENTER("Automata9Sum8")
	DBG_LOCAL(t_out,"out")
	DBG_LOCAL(t_in,"in")
	DBG_LOCAL(t_rules,"rules")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<126>");
	int t_iterations=t_out->m_settings->p_Get(String(L"laps",4))->m_value;
	DBG_LOCAL(t_iterations,"iterations")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<127>");
	int t_edge=t_out->m_settings->p_Get(String(L"edge",4))->m_value;
	DBG_LOCAL(t_edge,"edge")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<128>");
	Array<Array<int > > t_a=bb_box_Initialize2dArray(20,15);
	DBG_LOCAL(t_a,"a")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<130>");
	for(int t_x=0;t_x<20;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<131>");
		for(int t_y=0;t_y<15;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<132>");
			t_a.At(t_x).At(t_y)=t_in->m_state.At(t_x).At(t_y);
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<136>");
	while(t_iterations>=1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<137>");
		for(int t_x2=0;t_x2<20;t_x2=t_x2+1){
			DBG_BLOCK();
			DBG_LOCAL(t_x2,"x")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<138>");
			for(int t_y2=0;t_y2<15;t_y2=t_y2+1){
				DBG_BLOCK();
				DBG_LOCAL(t_y2,"y")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<139>");
				int t_total=0;
				DBG_LOCAL(t_total,"total")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<141>");
				for(int t_dx=-1;t_dx<=1;t_dx=t_dx+1){
					DBG_BLOCK();
					DBG_LOCAL(t_dx,"dx")
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<142>");
					for(int t_dy=-1;t_dy<=1;t_dy=t_dy+1){
						DBG_BLOCK();
						DBG_LOCAL(t_dy,"dy")
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<143>");
						if(!(t_dx==0 && t_dy==0)){
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<144>");
							int t_value=0;
							DBG_LOCAL(t_value,"value")
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<146>");
							if(t_x2+t_dx<20 && t_x2+t_dx>=0 && t_y2+t_dy<15 && t_y2+t_dy>=0){
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<147>");
								t_value=t_a.At(t_x2+t_dx).At(t_y2+t_dy);
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<149>");
								int t_2=t_edge;
								DBG_LOCAL(t_2,"2")
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<150>");
								if(t_2==0){
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<151>");
									t_value=0;
								}else{
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<152>");
									if(t_2==1){
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<153>");
										t_value=1;
									}else{
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<154>");
										if(t_2==2){
											DBG_BLOCK();
											DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<155>");
											int t_xx=(t_x2+t_dx+20) % 20;
											DBG_LOCAL(t_xx,"xx")
											DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<156>");
											int t_yy=(t_y2+t_dy+15) % 15;
											DBG_LOCAL(t_yy,"yy")
											DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<157>");
											bbPrint(String(L"xx: ",4)+String(t_xx)+String(L" yy: ",5)+String(t_yy));
											DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<158>");
											t_value=t_a.At(t_xx).At(t_yy);
										}
									}
								}
							}
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<163>");
							t_total=t_total+t_value;
						}
					}
				}
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<168>");
				t_out->m_state.At(t_x2).At(t_y2)=t_rules.At(t_total+t_a.At(t_x2).At(t_y2)*9);
			}
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<172>");
		t_iterations=t_iterations-1;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<174>");
		if(t_iterations>=1){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<175>");
			for(int t_x3=0;t_x3<20;t_x3=t_x3+1){
				DBG_BLOCK();
				DBG_LOCAL(t_x3,"x")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<176>");
				for(int t_y3=0;t_y3<15;t_y3=t_y3+1){
					DBG_BLOCK();
					DBG_LOCAL(t_y3,"y")
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<177>");
					t_a.At(t_x3).At(t_y3)=t_out->m_state.At(t_x3).At(t_y3);
				}
			}
		}
	}
}
void bb_functions_Smooth(c_Box* t_out,c_Box* t_in){
	DBG_ENTER("Smooth")
	DBG_LOCAL(t_out,"out")
	DBG_LOCAL(t_in,"in")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<205>");
	int t_[]={0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,1};
	bb_functions_Automata9Sum8(t_out,t_in,Array<int >(t_,18));
}
void bb_functions_Automata4(c_Box* t_out,c_Box* t_in,Array<int > t_rules){
	DBG_ENTER("Automata4")
	DBG_LOCAL(t_out,"out")
	DBG_LOCAL(t_in,"in")
	DBG_LOCAL(t_rules,"rules")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<85>");
	for(int t_x=0;t_x<20;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<86>");
		for(int t_y=0;t_y<15;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<87>");
			int t_total=0;
			DBG_LOCAL(t_total,"total")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<88>");
			int t_[]={0,1,0,-1};
			Array<int > t_dxs=Array<int >(t_,4);
			DBG_LOCAL(t_dxs,"dxs")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<89>");
			int t_2[]={-1,0,1,0};
			Array<int > t_dys=Array<int >(t_2,4);
			DBG_LOCAL(t_dys,"dys")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<91>");
			for(int t_d=0;t_d<=3;t_d=t_d+1){
				DBG_BLOCK();
				DBG_LOCAL(t_d,"d")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<92>");
				int t_dx=t_dxs.At(t_d);
				DBG_LOCAL(t_dx,"dx")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<93>");
				int t_dy=t_dys.At(t_d);
				DBG_LOCAL(t_dy,"dy")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<95>");
				if(!(t_dx==0 && t_dy==0) && t_x+t_dx<20 && t_x+t_dx>=0 && t_y+t_dy<15 && t_y+t_dy>=0){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<96>");
					t_total=t_total+t_in->m_state.At(t_x+t_dx).At(t_y+t_dy);
				}
			}
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<100>");
			t_out->m_state.At(t_x).At(t_y)=t_rules.At(t_total);
		}
	}
}
void bb_functions_Expand(c_Box* t_out,c_Box* t_in){
	DBG_ENTER("Expand")
	DBG_LOCAL(t_out,"out")
	DBG_LOCAL(t_in,"in")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<193>");
	int t_[]={0,1,1,1,1};
	bb_functions_Automata4(t_out,t_in,Array<int >(t_,5));
}
void bb_functions_Contract(c_Box* t_out,c_Box* t_in){
	DBG_ENTER("Contract")
	DBG_LOCAL(t_out,"out")
	DBG_LOCAL(t_in,"in")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<199>");
	int t_[]={0,0,0,0,1};
	bb_functions_Automata4(t_out,t_in,Array<int >(t_,5));
}
void bb_functions_Darken(c_Box* t_out,c_Box* t_lo,c_Box* t_hi){
	DBG_ENTER("Darken")
	DBG_LOCAL(t_out,"out")
	DBG_LOCAL(t_lo,"lo")
	DBG_LOCAL(t_hi,"hi")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<211>");
	for(int t_x=0;t_x<20;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<212>");
		for(int t_y=0;t_y<15;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<213>");
			if(t_hi->m_state.At(t_x).At(t_y)==1){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<214>");
				t_out->m_state.At(t_x).At(t_y)=t_lo->m_state.At(t_x).At(t_y);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<216>");
				t_out->m_state.At(t_x).At(t_y)=0;
			}
		}
	}
}
void bb_functions_Lighten(c_Box* t_out,c_Box* t_lo,c_Box* t_hi){
	DBG_ENTER("Lighten")
	DBG_LOCAL(t_out,"out")
	DBG_LOCAL(t_lo,"lo")
	DBG_LOCAL(t_hi,"hi")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<225>");
	for(int t_x=0;t_x<20;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<226>");
		for(int t_y=0;t_y<15;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<227>");
			if(t_hi->m_state.At(t_x).At(t_y)==0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<228>");
				t_out->m_state.At(t_x).At(t_y)=t_lo->m_state.At(t_x).At(t_y);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<230>");
				t_out->m_state.At(t_x).At(t_y)=1;
			}
		}
	}
}
void bb_functions_Invert(c_Box* t_out,c_Box* t_in){
	DBG_ENTER("Invert")
	DBG_LOCAL(t_out,"out")
	DBG_LOCAL(t_in,"in")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<239>");
	for(int t_x=0;t_x<20;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<240>");
		for(int t_y=0;t_y<15;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<241>");
			t_out->m_state.At(t_x).At(t_y)=1-t_in->m_state.At(t_x).At(t_y);
		}
	}
}
void bb_functions_Copy(c_Box* t_a,c_Box* t_b){
	DBG_ENTER("Copy")
	DBG_LOCAL(t_a,"a")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<249>");
	for(int t_x=0;t_x<20;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<250>");
		for(int t_y=0;t_y<15;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<251>");
			t_a->m_state.At(t_x).At(t_y)=t_b->m_state.At(t_x).At(t_y);
		}
	}
}
void bb_functions_ExecuteBox(c_Box* t_box,Array<c_Box* > t_in){
	DBG_ENTER("ExecuteBox")
	DBG_LOCAL(t_box,"box")
	DBG_LOCAL(t_in,"in")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<14>");
	String t_1=t_box->m_kind;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<15>");
	if(t_1==String(L"go",2)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<16>");
		bb_functions_Clear();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<18>");
		c_Enumerator5* t_=bb_main_APP->p_patch()->m_boxes->p_ObjectEnumerator();
		while(t_->p_HasNext()){
			DBG_BLOCK();
			c_Box* t_root=t_->p_NextObject();
			DBG_LOCAL(t_root,"root")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<19>");
			if(t_root->m_ins==0 && t_root->m_kind!=String(L"go",2)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<20>");
				t_root->p_Execute();
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<23>");
		if(t_1==String(L"clear",5)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<24>");
			bb_functions_Clear();
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<25>");
			if(t_1==String(L"noise",5)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<26>");
				bb_functions_Noise(t_box);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<27>");
				if(t_1==String(L"automata",8)){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<28>");
					bb_functions_Automata9Sum8(t_box,t_in.At(0),bb_gadgets_ruleTables->p_Get2(t_box->m_settings->p_Get(String(L"rules",5))->m_value)->m_value);
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<29>");
					if(t_1==String(L"conway",6)){
						DBG_BLOCK();
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<30>");
						if(t_1==String(L"smooth",6)){
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<31>");
							bb_functions_Smooth(t_box,t_in.At(0));
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<32>");
							if(t_1==String(L"expand",6)){
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<33>");
								bb_functions_Expand(t_box,t_in.At(0));
							}else{
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<34>");
								if(t_1==String(L"contract",8)){
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<35>");
									bb_functions_Contract(t_box,t_in.At(0));
								}else{
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<36>");
									if(t_1==String(L"darken",6)){
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<37>");
										bb_functions_Darken(t_box,t_in.At(0),t_in.At(1));
									}else{
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<38>");
										if(t_1==String(L"lighten",7)){
											DBG_BLOCK();
											DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<39>");
											bb_functions_Lighten(t_box,t_in.At(0),t_in.At(1));
										}else{
											DBG_BLOCK();
											DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<40>");
											if(t_1==String(L"invert",6)){
												DBG_BLOCK();
												DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<41>");
												bb_functions_Invert(t_box,t_in.At(0));
											}else{
												DBG_BLOCK();
												DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<42>");
												if(t_1==String(L"fill",4)){
													DBG_BLOCK();
													DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<43>");
													bb_functions_Fill(t_box,t_box->m_settings->p_Get(String(L"color",5))->m_value);
												}else{
													DBG_BLOCK();
													DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<44>");
													if(t_1==String(L"view",4)){
														DBG_BLOCK();
														DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/functions.monkey<45>");
														bb_functions_Copy(t_box,t_in.At(0));
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}
void bb_main_MakeSparks(c_Box* t_box){
	DBG_ENTER("MakeSparks")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<152>");
	c_Enumerator4* t_=bb_main_APP->p_patch()->m_wires->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Wire* t_wire=t_->p_NextObject();
		DBG_LOCAL(t_wire,"wire")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<153>");
		if(t_wire->m_a==t_box){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<154>");
			bb_main_APP->p_patch()->m_sparks->p_AddLast4((new c_Spark)->m_new(t_wire));
		}
	}
}
int bb_input_KeyHit(int t_key){
	DBG_ENTER("KeyHit")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/input.monkey<44>");
	int t_=bb_input_device->p_KeyHit(t_key);
	return t_;
}
c_View::c_View(){
	m_box=0;
}
c_Gadget* c_View::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("View.HandleEvent")
	c_View *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<125>");
	c_Gadget* t_=(this);
	return t_;
}
void c_View::p_OnRender(){
	DBG_ENTER("View.OnRender")
	c_View *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<129>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<130>");
	bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<131>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<132>");
	bb_graphics_DrawRect(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<134>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<135>");
	m_box->p_Render();
}
c_View* c_View::m_new(int t_x,int t_y,int t_w,int t_h){
	DBG_ENTER("View.new")
	c_View *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<119>");
	c_Gadget::m_new();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<120>");
	this->m_x=t_x;
	this->m_y=t_y;
	this->m_w=t_w;
	this->m_h=t_h;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<121>");
	gc_assign(m_box,((new c_ViewBox)->m_new(8,8)));
	return this;
}
c_View* c_View::m_new2(){
	DBG_ENTER("View.new")
	c_View *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<116>");
	c_Gadget::m_new();
	return this;
}
void c_View::mark(){
	c_Gadget::mark();
	gc_mark_q(m_box);
}
String c_View::debug(){
	String t="(View)\n";
	t=c_Gadget::debug()+t;
	t+=dbg_decl("box",&m_box);
	return t;
}
int bb_graphics_DebugRenderDevice(){
	DBG_ENTER("DebugRenderDevice")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<53>");
	if(!((bb_graphics_renderDevice)!=0)){
		DBG_BLOCK();
		bbError(String(L"Rendering operations can only be performed inside OnRender",58));
	}
	return 0;
}
int bb_graphics_Cls(Float t_r,Float t_g,Float t_b){
	DBG_ENTER("Cls")
	DBG_LOCAL(t_r,"r")
	DBG_LOCAL(t_g,"g")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<378>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<380>");
	bb_graphics_renderDevice->Cls(t_r,t_g,t_b);
	return 0;
}
int bb_graphics_PushMatrix(){
	DBG_ENTER("PushMatrix")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<333>");
	int t_sp=bb_graphics_context->m_matrixSp;
	DBG_LOCAL(t_sp,"sp")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<334>");
	if(t_sp==bb_graphics_context->m_matrixStack.Length()){
		DBG_BLOCK();
		gc_assign(bb_graphics_context->m_matrixStack,bb_graphics_context->m_matrixStack.Resize(t_sp*2));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<335>");
	bb_graphics_context->m_matrixStack.At(t_sp+0)=bb_graphics_context->m_ix;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<336>");
	bb_graphics_context->m_matrixStack.At(t_sp+1)=bb_graphics_context->m_iy;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<337>");
	bb_graphics_context->m_matrixStack.At(t_sp+2)=bb_graphics_context->m_jx;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<338>");
	bb_graphics_context->m_matrixStack.At(t_sp+3)=bb_graphics_context->m_jy;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<339>");
	bb_graphics_context->m_matrixStack.At(t_sp+4)=bb_graphics_context->m_tx;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<340>");
	bb_graphics_context->m_matrixStack.At(t_sp+5)=bb_graphics_context->m_ty;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<341>");
	bb_graphics_context->m_matrixSp=t_sp+6;
	return 0;
}
int bb_graphics_Transform(Float t_ix,Float t_iy,Float t_jx,Float t_jy,Float t_tx,Float t_ty){
	DBG_ENTER("Transform")
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_LOCAL(t_jx,"jx")
	DBG_LOCAL(t_jy,"jy")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<355>");
	Float t_ix2=t_ix*bb_graphics_context->m_ix+t_iy*bb_graphics_context->m_jx;
	DBG_LOCAL(t_ix2,"ix2")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<356>");
	Float t_iy2=t_ix*bb_graphics_context->m_iy+t_iy*bb_graphics_context->m_jy;
	DBG_LOCAL(t_iy2,"iy2")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<357>");
	Float t_jx2=t_jx*bb_graphics_context->m_ix+t_jy*bb_graphics_context->m_jx;
	DBG_LOCAL(t_jx2,"jx2")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<358>");
	Float t_jy2=t_jx*bb_graphics_context->m_iy+t_jy*bb_graphics_context->m_jy;
	DBG_LOCAL(t_jy2,"jy2")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<359>");
	Float t_tx2=t_tx*bb_graphics_context->m_ix+t_ty*bb_graphics_context->m_jx+bb_graphics_context->m_tx;
	DBG_LOCAL(t_tx2,"tx2")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<360>");
	Float t_ty2=t_tx*bb_graphics_context->m_iy+t_ty*bb_graphics_context->m_jy+bb_graphics_context->m_ty;
	DBG_LOCAL(t_ty2,"ty2")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<361>");
	bb_graphics_SetMatrix(t_ix2,t_iy2,t_jx2,t_jy2,t_tx2,t_ty2);
	return 0;
}
int bb_graphics_Transform2(Array<Float > t_m){
	DBG_ENTER("Transform")
	DBG_LOCAL(t_m,"m")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<351>");
	bb_graphics_Transform(t_m.At(0),t_m.At(1),t_m.At(2),t_m.At(3),t_m.At(4),t_m.At(5));
	return 0;
}
int bb_graphics_Translate(Float t_x,Float t_y){
	DBG_ENTER("Translate")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<365>");
	bb_graphics_Transform(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),t_x,t_y);
	return 0;
}
int bb_graphics_PopMatrix(){
	DBG_ENTER("PopMatrix")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<345>");
	int t_sp=bb_graphics_context->m_matrixSp-6;
	DBG_LOCAL(t_sp,"sp")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<346>");
	bb_graphics_SetMatrix(bb_graphics_context->m_matrixStack.At(t_sp+0),bb_graphics_context->m_matrixStack.At(t_sp+1),bb_graphics_context->m_matrixStack.At(t_sp+2),bb_graphics_context->m_matrixStack.At(t_sp+3),bb_graphics_context->m_matrixStack.At(t_sp+4),bb_graphics_context->m_matrixStack.At(t_sp+5));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<347>");
	bb_graphics_context->m_matrixSp=t_sp;
	return 0;
}
bool bb_gui__scissorEnabled;
c_ScissorBox::c_ScissorBox(){
	m_value=Array<Float >();
}
c_ScissorBox* c_ScissorBox::m_new(Array<Float > t_value){
	DBG_ENTER("ScissorBox.new")
	c_ScissorBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<81>");
	gc_assign(this->m_value,t_value);
	return this;
}
c_ScissorBox* c_ScissorBox::m_new2(){
	DBG_ENTER("ScissorBox.new")
	c_ScissorBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<77>");
	return this;
}
void c_ScissorBox::mark(){
	Object::mark();
	gc_mark_q(m_value);
}
String c_ScissorBox::debug(){
	String t="(ScissorBox)\n";
	t+=dbg_decl("value",&m_value);
	return t;
}
c_List7::c_List7(){
	m__head=((new c_HeadNode7)->m_new());
}
c_List7* c_List7::m_new(){
	DBG_ENTER("List.new")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node10* c_List7::p_AddLast7(c_ScissorBox* t_data){
	DBG_ENTER("List.AddLast")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node10* t_=(new c_Node10)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List7* c_List7::m_new2(Array<c_ScissorBox* > t_data){
	DBG_ENTER("List.new")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<c_ScissorBox* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_ScissorBox* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast7(t_t);
	}
	return this;
}
bool c_List7::p_IsEmpty(){
	DBG_ENTER("List.IsEmpty")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<50>");
	bool t_=m__head->m__succ==m__head;
	return t_;
}
c_ScissorBox* c_List7::p_RemoveLast(){
	DBG_ENTER("List.RemoveLast")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<96>");
	if(p_IsEmpty()){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on empty list",31));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<98>");
	c_ScissorBox* t_data=m__head->m__pred->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<99>");
	m__head->m__pred->p_Remove2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<100>");
	return t_data;
}
bool c_List7::p_Equals6(c_ScissorBox* t_lhs,c_ScissorBox* t_rhs){
	DBG_ENTER("List.Equals")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
c_Node10* c_List7::p_FindLast3(c_ScissorBox* t_value,c_Node10* t_start){
	DBG_ENTER("List.FindLast")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_start,"start")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<128>");
	while(t_start!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<129>");
		if(p_Equals6(t_value,t_start->m__data)){
			DBG_BLOCK();
			return t_start;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<130>");
		t_start=t_start->m__pred;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<132>");
	return 0;
}
c_Node10* c_List7::p_FindLast4(c_ScissorBox* t_value){
	DBG_ENTER("List.FindLast")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<124>");
	c_Node10* t_=p_FindLast3(t_value,m__head->m__pred);
	return t_;
}
void c_List7::p_RemoveLast3(c_ScissorBox* t_value){
	DBG_ENTER("List.RemoveLast")
	c_List7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<146>");
	c_Node10* t_node=p_FindLast4(t_value);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<147>");
	if((t_node)!=0){
		DBG_BLOCK();
		t_node->p_Remove2();
	}
}
void c_List7::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List7::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node10::c_Node10(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node10* c_Node10::m_new(c_Node10* t_succ,c_Node10* t_pred,c_ScissorBox* t_data){
	DBG_ENTER("Node.new")
	c_Node10 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node10* c_Node10::m_new2(){
	DBG_ENTER("Node.new")
	c_Node10 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node10::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node10 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node10::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node10::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode7::c_HeadNode7(){
}
c_HeadNode7* c_HeadNode7::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode7 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node10::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode7::mark(){
	c_Node10::mark();
}
String c_HeadNode7::debug(){
	String t="(HeadNode)\n";
	t=c_Node10::debug()+t;
	return t;
}
c_List7* bb_gui__scissors;
void bb_gui_PopScissor(){
	DBG_ENTER("PopScissor")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<69>");
	if(!bb_gui__scissorEnabled){
		DBG_BLOCK();
		bb_gui_EnableScissor();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<70>");
	c_ScissorBox* t__s=bb_gui__scissors->p_RemoveLast();
	DBG_LOCAL(t__s,"_s")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<71>");
	Array<Float > t_s=t__s->m_value;
	DBG_LOCAL(t_s,"s")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<72>");
	bb_graphics_SetScissor(t_s.At(0),t_s.At(1),t_s.At(2),t_s.At(3));
}
void bb_gui_EnableScissor(){
	DBG_ENTER("EnableScissor")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<44>");
	if(bb_gui__scissorEnabled){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<45>");
	bb_gui__scissorEnabled=true;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<46>");
	bb_gui_PopScissor();
}
Array<Float > bb_graphics_GetScissor(){
	DBG_ENTER("GetScissor")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<297>");
	Float t_2[]={bb_graphics_context->m_scissor_x,bb_graphics_context->m_scissor_y,bb_graphics_context->m_scissor_width,bb_graphics_context->m_scissor_height};
	Array<Float > t_=Array<Float >(t_2,4);
	return t_;
}
int bb_graphics_GetScissor2(Array<Float > t_scissor){
	DBG_ENTER("GetScissor")
	DBG_LOCAL(t_scissor,"scissor")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<301>");
	t_scissor.At(0)=bb_graphics_context->m_scissor_x;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<302>");
	t_scissor.At(1)=bb_graphics_context->m_scissor_y;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<303>");
	t_scissor.At(2)=bb_graphics_context->m_scissor_width;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<304>");
	t_scissor.At(3)=bb_graphics_context->m_scissor_height;
	return 0;
}
void bb_gui_PushScissor(){
	DBG_ENTER("PushScissor")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<61>");
	if(!bb_gui__scissorEnabled){
		DBG_BLOCK();
		bb_gui_EnableScissor();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<62>");
	Array<Float > t_s=bb_graphics_GetScissor();
	DBG_LOCAL(t_s,"s")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<63>");
	bb_gui__scissors->p_AddLast7((new c_ScissorBox)->m_new(t_s));
}
Array<Float > bb_graphics_GetMatrix(){
	DBG_ENTER("GetMatrix")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<323>");
	Float t_2[]={bb_graphics_context->m_ix,bb_graphics_context->m_iy,bb_graphics_context->m_jx,bb_graphics_context->m_jy,bb_graphics_context->m_tx,bb_graphics_context->m_ty};
	Array<Float > t_=Array<Float >(t_2,6);
	return t_;
}
int bb_graphics_GetMatrix2(Array<Float > t_matrix){
	DBG_ENTER("GetMatrix")
	DBG_LOCAL(t_matrix,"matrix")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<327>");
	t_matrix.At(0)=bb_graphics_context->m_ix;
	t_matrix.At(1)=bb_graphics_context->m_iy;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<328>");
	t_matrix.At(2)=bb_graphics_context->m_jx;
	t_matrix.At(3)=bb_graphics_context->m_jy;
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<329>");
	t_matrix.At(4)=bb_graphics_context->m_tx;
	t_matrix.At(5)=bb_graphics_context->m_ty;
	return 0;
}
int bb_math_Max(int t_x,int t_y){
	DBG_ENTER("Max")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/math.monkey<56>");
	if(t_x>t_y){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/math.monkey<57>");
	return t_y;
}
Float bb_math_Max2(Float t_x,Float t_y){
	DBG_ENTER("Max")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/math.monkey<83>");
	if(t_x>t_y){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/math.monkey<84>");
	return t_y;
}
int bb_math_Min(int t_x,int t_y){
	DBG_ENTER("Min")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/math.monkey<51>");
	if(t_x<t_y){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/math.monkey<52>");
	return t_y;
}
Float bb_math_Min2(Float t_x,Float t_y){
	DBG_ENTER("Min")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/math.monkey<78>");
	if(t_x<t_y){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/math.monkey<79>");
	return t_y;
}
Array<int > bb_gui_RectangleUnion(int t_ax,int t_ay,int t_aw,int t_ah,int t_bx,int t_by,int t_bw,int t_bh){
	DBG_ENTER("RectangleUnion")
	DBG_LOCAL(t_ax,"ax")
	DBG_LOCAL(t_ay,"ay")
	DBG_LOCAL(t_aw,"aw")
	DBG_LOCAL(t_ah,"ah")
	DBG_LOCAL(t_bx,"bx")
	DBG_LOCAL(t_by,"by")
	DBG_LOCAL(t_bw,"bw")
	DBG_LOCAL(t_bh,"bh")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<29>");
	int t_x0=bb_math_Max(t_ax,t_bx);
	DBG_LOCAL(t_x0,"x0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<30>");
	int t_y0=bb_math_Max(t_ay,t_by);
	DBG_LOCAL(t_y0,"y0")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<31>");
	int t_x1=bb_math_Min(t_ax+t_aw-1,t_bx+t_bw-1);
	DBG_LOCAL(t_x1,"x1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<32>");
	int t_y1=bb_math_Min(t_ay+t_ah-1,t_by+t_bh-1);
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<33>");
	int t_2[]={t_x0,t_y0,t_x1-t_x0+1,t_y1-t_y0+1};
	Array<int > t_=Array<int >(t_2,4);
	return t_;
}
c_TabGadget::c_TabGadget(){
	m_locked=true;
	m_chosen=false;
	m_project=0;
	m_closeButton=0;
	m_saveButton=0;
	m_name=String();
}
c_Gadget* c_TabGadget::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("TabGadget.HandleEvent")
	c_TabGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<168>");
	c_Gadget* t_gadget=c_ContainerGadget::p_HandleEvent(t_event);
	DBG_LOCAL(t_gadget,"gadget")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<169>");
	if(t_gadget!=(this)){
		DBG_BLOCK();
		return t_gadget;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<171>");
	int t_1=t_event->m_id;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<172>");
	if(t_1==1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<173>");
		m_locked=false;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<174>");
		m_parent->p_HandleGadgetEvent((new c_GadgetEvent)->m_new(this));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<175>");
		bb_main_sx=m_x;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<176>");
		if(t_1==3){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<177>");
			m_x=bb_math_Max(4,bb_main_sx+t_event->m_dx);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<178>");
			if(t_1==5){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<179>");
				m_locked=true;
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<183>");
	c_Gadget* t_=(this);
	return t_;
}
void c_TabGadget::p_HandleGadgetEvent(c_GadgetEvent* t_event){
	DBG_ENTER("TabGadget.HandleGadgetEvent")
	c_TabGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<187>");
	if(t_event->m_source==(m_closeButton)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<188>");
		dynamic_cast<c_Browser*>(m_parent)->p_RemoveTab(this);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<189>");
		if(t_event->m_source==(m_saveButton)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<190>");
			String t_path=diddy::savefilename();
		}
	}
}
void c_TabGadget::p_OnRender(){
	DBG_ENTER("TabGadget.OnRender")
	c_TabGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<195>");
	String t_name=this->m_name;
	DBG_LOCAL(t_name,"name")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<196>");
	if(m_chosen){
		DBG_BLOCK();
		t_name=t_name+String(L"*",1);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<197>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<198>");
	bb_main_imgTab->p_Draw(0,0,m_w,m_h);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<199>");
	bb_graphics_DrawText(t_name,FLOAT(8.0),FLOAT(2.0),FLOAT(0.0),FLOAT(0.0));
}
c_TabGadget* c_TabGadget::m_new(String t_path){
	DBG_ENTER("TabGadget.new")
	c_TabGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<156>");
	c_ContainerGadget::m_new2();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<157>");
	this->m_name=bb_os_StripAll(t_path);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<158>");
	m_w=int(bb_graphics_TextWidth(m_name)+FLOAT(18.0)+FLOAT(34.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<159>");
	m_h=18;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<160>");
	gc_assign(m_saveButton,(new c_BrowserButton)->m_new(int(FLOAT(8.0)+bb_graphics_TextWidth(this->m_name)+FLOAT(11.0)),5,bb_main_imgSave));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<161>");
	gc_assign(m_closeButton,(new c_BrowserButton)->m_new(int(FLOAT(8.0)+bb_graphics_TextWidth(m_name)+FLOAT(11.0)+FLOAT(9.0)+FLOAT(5.0)),5,bb_main_imgClose));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<162>");
	p_AddChild(m_saveButton);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<163>");
	p_AddChild(m_closeButton);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<164>");
	gc_assign(m_project,(new c_Project)->m_new(t_path));
	return this;
}
c_TabGadget* c_TabGadget::m_new2(){
	DBG_ENTER("TabGadget.new")
	c_TabGadget *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/browser.monkey<147>");
	c_ContainerGadget::m_new2();
	return this;
}
void c_TabGadget::mark(){
	c_ContainerGadget::mark();
	gc_mark_q(m_project);
	gc_mark_q(m_closeButton);
	gc_mark_q(m_saveButton);
}
String c_TabGadget::debug(){
	String t="(TabGadget)\n";
	t=c_ContainerGadget::debug()+t;
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("locked",&m_locked);
	t+=dbg_decl("chosen",&m_chosen);
	t+=dbg_decl("saveButton",&m_saveButton);
	t+=dbg_decl("closeButton",&m_closeButton);
	t+=dbg_decl("project",&m_project);
	return t;
}
c_List8::c_List8(){
	m__head=((new c_HeadNode8)->m_new());
}
c_List8* c_List8::m_new(){
	DBG_ENTER("List.new")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node11* c_List8::p_AddLast8(c_TabGadget* t_data){
	DBG_ENTER("List.AddLast")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node11* t_=(new c_Node11)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List8* c_List8::m_new2(Array<c_TabGadget* > t_data){
	DBG_ENTER("List.new")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<c_TabGadget* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_TabGadget* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast8(t_t);
	}
	return this;
}
bool c_List8::p_IsEmpty(){
	DBG_ENTER("List.IsEmpty")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<50>");
	bool t_=m__head->m__succ==m__head;
	return t_;
}
c_Enumerator6* c_List8::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<186>");
	c_Enumerator6* t_=(new c_Enumerator6)->m_new(this);
	return t_;
}
bool c_List8::p_Equals7(c_TabGadget* t_lhs,c_TabGadget* t_rhs){
	DBG_ENTER("List.Equals")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
int c_List8::p_RemoveEach5(c_TabGadget* t_value){
	DBG_ENTER("List.RemoveEach")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<151>");
	c_Node11* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<152>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<153>");
		c_Node11* t_succ=t_node->m__succ;
		DBG_LOCAL(t_succ,"succ")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<154>");
		if(p_Equals7(t_node->m__data,t_value)){
			DBG_BLOCK();
			t_node->p_Remove2();
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<155>");
		t_node=t_succ;
	}
	return 0;
}
c_Node11* c_List8::p_AddFirst2(c_TabGadget* t_data){
	DBG_ENTER("List.AddFirst")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<104>");
	c_Node11* t_=(new c_Node11)->m_new(m__head->m__succ,m__head,t_data);
	return t_;
}
c_BackwardsList2* c_List8::p_Backwards(){
	DBG_ENTER("List.Backwards")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<190>");
	c_BackwardsList2* t_=(new c_BackwardsList2)->m_new(this);
	return t_;
}
c_TabGadget* c_List8::p_First(){
	DBG_ENTER("List.First")
	c_List8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<73>");
	if(p_IsEmpty()){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on empty list",31));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<75>");
	return m__head->m__succ->m__data;
}
void c_List8::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List8::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node11::c_Node11(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node11* c_Node11::m_new(c_Node11* t_succ,c_Node11* t_pred,c_TabGadget* t_data){
	DBG_ENTER("Node.new")
	c_Node11 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node11* c_Node11::m_new2(){
	DBG_ENTER("Node.new")
	c_Node11 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node11::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node11 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node11::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node11::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode8::c_HeadNode8(){
}
c_HeadNode8* c_HeadNode8::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode8 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node11::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode8::mark(){
	c_Node11::mark();
}
String c_HeadNode8::debug(){
	String t="(HeadNode)\n";
	t=c_Node11::debug()+t;
	return t;
}
c_Enumerator6::c_Enumerator6(){
	m__list=0;
	m__curr=0;
}
c_Enumerator6* c_Enumerator6::m_new(c_List8* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator6* c_Enumerator6::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator6::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_TabGadget* c_Enumerator6::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator6 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<338>");
	c_TabGadget* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator6::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator6::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
c_BackwardsList2::c_BackwardsList2(){
	m__list=0;
}
c_BackwardsList2* c_BackwardsList2::m_new(c_List8* t_list){
	DBG_ENTER("BackwardsList.new")
	c_BackwardsList2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<353>");
	gc_assign(m__list,t_list);
	return this;
}
c_BackwardsList2* c_BackwardsList2::m_new2(){
	DBG_ENTER("BackwardsList.new")
	c_BackwardsList2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<350>");
	return this;
}
c_BackwardsEnumerator2* c_BackwardsList2::p_ObjectEnumerator(){
	DBG_ENTER("BackwardsList.ObjectEnumerator")
	c_BackwardsList2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<357>");
	c_BackwardsEnumerator2* t_=(new c_BackwardsEnumerator2)->m_new(m__list);
	return t_;
}
void c_BackwardsList2::mark(){
	Object::mark();
	gc_mark_q(m__list);
}
String c_BackwardsList2::debug(){
	String t="(BackwardsList)\n";
	t+=dbg_decl("_list",&m__list);
	return t;
}
c_BackwardsEnumerator2::c_BackwardsEnumerator2(){
	m__list=0;
	m__curr=0;
}
c_BackwardsEnumerator2* c_BackwardsEnumerator2::m_new(c_List8* t_list){
	DBG_ENTER("BackwardsEnumerator.new")
	c_BackwardsEnumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<369>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<370>");
	gc_assign(m__curr,t_list->m__head->m__pred);
	return this;
}
c_BackwardsEnumerator2* c_BackwardsEnumerator2::m_new2(){
	DBG_ENTER("BackwardsEnumerator.new")
	c_BackwardsEnumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<366>");
	return this;
}
bool c_BackwardsEnumerator2::p_HasNext(){
	DBG_ENTER("BackwardsEnumerator.HasNext")
	c_BackwardsEnumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<374>");
	while(m__curr->m__pred->m__succ!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<375>");
		gc_assign(m__curr,m__curr->m__pred);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<377>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_TabGadget* c_BackwardsEnumerator2::p_NextObject(){
	DBG_ENTER("BackwardsEnumerator.NextObject")
	c_BackwardsEnumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<381>");
	c_TabGadget* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<382>");
	gc_assign(m__curr,m__curr->m__pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<383>");
	return t_data;
}
void c_BackwardsEnumerator2::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_BackwardsEnumerator2::debug(){
	String t="(BackwardsEnumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
int bb_graphics_DrawLine(Float t_x1,Float t_y1,Float t_x2,Float t_y2){
	DBG_ENTER("DrawLine")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_LOCAL(t_x2,"x2")
	DBG_LOCAL(t_y2,"y2")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<401>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<403>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<404>");
	bb_graphics_renderDevice->DrawLine(t_x1,t_y1,t_x2,t_y2);
	return 0;
}
c_GadgetEvent::c_GadgetEvent(){
	m_source=0;
}
c_GadgetEvent* c_GadgetEvent::m_new(c_Gadget* t_source){
	DBG_ENTER("GadgetEvent.new")
	c_GadgetEvent *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_source,"source")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadgetevent.monkey<13>");
	gc_assign(this->m_source,t_source);
	return this;
}
c_GadgetEvent* c_GadgetEvent::m_new2(){
	DBG_ENTER("GadgetEvent.new")
	c_GadgetEvent *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gadgetevent.monkey<9>");
	return this;
}
void c_GadgetEvent::mark(){
	Object::mark();
	gc_mark_q(m_source);
}
String c_GadgetEvent::debug(){
	String t="(GadgetEvent)\n";
	t+=dbg_decl("source",&m_source);
	return t;
}
int bb_graphics_DrawRect(Float t_x,Float t_y,Float t_w,Float t_h){
	DBG_ENTER("DrawRect")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<393>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<395>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<396>");
	bb_graphics_renderDevice->DrawRect(t_x,t_y,t_w,t_h);
	return 0;
}
int bb_graphics_DrawImage(c_Image* t_image,Float t_x,Float t_y,int t_frame){
	DBG_ENTER("DrawImage")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<451>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<452>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<455>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<457>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<459>");
	if((t_image->m_flags&65536)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<460>");
		bb_graphics_renderDevice->DrawSurface(t_image->m_surface,t_x-t_image->m_tx,t_y-t_image->m_ty);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<462>");
		bb_graphics_renderDevice->DrawSurface2(t_image->m_surface,t_x-t_image->m_tx,t_y-t_image->m_ty,t_f->m_x,t_f->m_y,t_image->m_width,t_image->m_height);
	}
	return 0;
}
int bb_graphics_Rotate(Float t_angle){
	DBG_ENTER("Rotate")
	DBG_LOCAL(t_angle,"angle")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<373>");
	bb_graphics_Transform((Float)cos((t_angle)*D2R),-(Float)sin((t_angle)*D2R),(Float)sin((t_angle)*D2R),(Float)cos((t_angle)*D2R),FLOAT(0.0),FLOAT(0.0));
	return 0;
}
int bb_graphics_Scale(Float t_x,Float t_y){
	DBG_ENTER("Scale")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<369>");
	bb_graphics_Transform(t_x,FLOAT(0.0),FLOAT(0.0),t_y,FLOAT(0.0),FLOAT(0.0));
	return 0;
}
int bb_graphics_DrawImage2(c_Image* t_image,Float t_x,Float t_y,Float t_rotation,Float t_scaleX,Float t_scaleY,int t_frame){
	DBG_ENTER("DrawImage")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_rotation,"rotation")
	DBG_LOCAL(t_scaleX,"scaleX")
	DBG_LOCAL(t_scaleY,"scaleY")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<469>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<470>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<473>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<475>");
	bb_graphics_PushMatrix();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<477>");
	bb_graphics_Translate(t_x,t_y);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<478>");
	bb_graphics_Rotate(t_rotation);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<479>");
	bb_graphics_Scale(t_scaleX,t_scaleY);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<481>");
	bb_graphics_Translate(-t_image->m_tx,-t_image->m_ty);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<483>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<485>");
	if((t_image->m_flags&65536)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<486>");
		bb_graphics_renderDevice->DrawSurface(t_image->m_surface,FLOAT(0.0),FLOAT(0.0));
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<488>");
		bb_graphics_renderDevice->DrawSurface2(t_image->m_surface,FLOAT(0.0),FLOAT(0.0),t_f->m_x,t_f->m_y,t_image->m_width,t_image->m_height);
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<491>");
	bb_graphics_PopMatrix();
	return 0;
}
int bb_main__dragMode;
int bb_main_sx;
int bb_main_sy;
c_Box* bb_main_from;
void bb_main_DeleteWire(c_Wire* t_wire){
	DBG_ENTER("DeleteWire")
	DBG_LOCAL(t_wire,"wire")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<178>");
	if(t_wire==0){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<180>");
	c_Enumerator3* t_=bb_main_APP->p_patch()->m_sparks->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Spark* t_spark=t_->p_NextObject();
		DBG_LOCAL(t_spark,"spark")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<181>");
		if(t_spark->m_wire==t_wire){
			DBG_BLOCK();
			bb_main_APP->p_patch()->m_sparks->p_RemoveEach2(t_spark);
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<184>");
	bb_main_APP->p_patch()->m_wires->p_Remove4(t_wire);
}
c_Panel::c_Panel(){
	m_yMax=50;
}
void c_Panel::p_AddChild(c_Gadget* t_child){
	DBG_ENTER("Panel.AddChild")
	c_Panel *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<17>");
	c_ContainerGadget::p_AddChild(t_child);
}
c_Gadget* c_Panel::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("Panel.HandleEvent")
	c_Panel *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<21>");
	c_Gadget* t_gadget=c_ContainerGadget::p_HandleEvent(t_event);
	DBG_LOCAL(t_gadget,"gadget")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<22>");
	if(t_gadget!=(this)){
		DBG_BLOCK();
		return t_gadget;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<24>");
	int t_1=t_event->m_id;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<25>");
	if(t_1==4){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<26>");
		m_oy=bb_main_sy+t_event->m_dy;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<27>");
		m_oy=-bb_math_Max(0,bb_math_Min(-m_oy,m_yMax));
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<28>");
		if(t_1==2){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<29>");
			bb_main_sy=m_oy;
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<33>");
	c_Gadget* t_=(this);
	return t_;
}
void c_Panel::p__ExecuteBoxSelectedIfSatisfied(){
	DBG_ENTER("Panel._ExecuteBoxSelectedIfSatisfied")
	c_Panel *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<61>");
	Array<bool > t_satisfied=Array<bool >(bb_main_APP->p_project()->m_boxSelected->m_ins);
	DBG_LOCAL(t_satisfied,"satisfied")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<63>");
	c_Enumerator4* t_=bb_main_APP->p_patch()->m_wires->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Wire* t_wire=t_->p_NextObject();
		DBG_LOCAL(t_wire,"wire")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<64>");
		if(t_wire->m_b==bb_main_APP->p_project()->m_boxSelected && t_wire->m_a->m_done){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<65>");
			t_satisfied.At(t_wire->m_bId)=true;
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<69>");
	for(int t_n=0;t_n<t_satisfied.Length();t_n=t_n+1){
		DBG_BLOCK();
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<70>");
		if(t_satisfied.At(t_n)==false){
			DBG_BLOCK();
			return;
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<73>");
	bb_main_APP->p_project()->m_boxSelected->p_Execute();
}
void c_Panel::p_HandleGadgetEvent(c_GadgetEvent* t_event){
	DBG_ENTER("Panel.HandleGadgetEvent")
	c_Panel *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<37>");
	bool t_2=true;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<38>");
	if(t_2==(dynamic_cast<c_Slider*>(t_event->m_source)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<39>");
		c_Slider* t_s=dynamic_cast<c_Slider*>(t_event->m_source);
		DBG_LOCAL(t_s,"s")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<40>");
		bb_main_APP->p_project()->m_boxSelected->m_settings->p_Get(t_s->m_name)->m_value=t_s->m_index;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<41>");
		p__ExecuteBoxSelectedIfSatisfied();
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<42>");
		if(t_2==(dynamic_cast<c_NumberBox*>(t_event->m_source)!=0)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<43>");
			c_NumberBox* t_n=dynamic_cast<c_NumberBox*>(t_event->m_source);
			DBG_LOCAL(t_n,"n")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<44>");
			bb_main_APP->p_project()->m_boxSelected->m_settings->p_Get(t_n->m_name)->m_value=t_n->m_value;
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<45>");
			p__ExecuteBoxSelectedIfSatisfied();
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<46>");
			if(t_2==(dynamic_cast<c_DropList*>(t_event->m_source)!=0)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<47>");
				c_DropList* t_d=dynamic_cast<c_DropList*>(t_event->m_source);
				DBG_LOCAL(t_d,"d")
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<48>");
				bb_main_APP->p_project()->m_boxSelected->m_settings->p_Get(t_d->m_name)->m_value=t_d->m_index;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<49>");
				p__ExecuteBoxSelectedIfSatisfied();
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<50>");
				if(t_2==(dynamic_cast<c_CheckBox*>(t_event->m_source)!=0)){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<51>");
					c_CheckBox* t_c=dynamic_cast<c_CheckBox*>(t_event->m_source);
					DBG_LOCAL(t_c,"c")
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<52>");
					bb_main_APP->p_project()->m_boxSelected->m_settings->p_Get(t_c->m_name)->m_value=t_c->m_on;
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<53>");
					p__ExecuteBoxSelectedIfSatisfied();
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<54>");
					if(t_2==(dynamic_cast<c_RuleTable*>(t_event->m_source)!=0)){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<55>");
						p__ExecuteBoxSelectedIfSatisfied();
					}
				}
			}
		}
	}
}
void c_Panel::p_OnRender(){
	DBG_ENTER("Panel.OnRender")
	c_Panel *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<77>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<78>");
	bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<79>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<80>");
	bb_graphics_DrawRect(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
}
void c_Panel::p_OnRenderInterior(){
	DBG_ENTER("Panel.OnRenderInterior")
	c_Panel *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<84>");
	int t_y=4;
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<86>");
	c_Enumerator2* t_=m_children->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Gadget* t_child=t_->p_NextObject();
		DBG_LOCAL(t_child,"child")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<87>");
		t_child->m_y=t_y;
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<88>");
		t_y+=t_child->m_h+4;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<91>");
	c_ContainerGadget::p_OnRenderInterior();
}
c_Panel* c_Panel::m_new(int t_x,int t_y,int t_w,int t_h){
	DBG_ENTER("Panel.new")
	c_Panel *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<12>");
	c_ContainerGadget::m_new2();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<13>");
	this->m_x=t_x;
	this->m_y=t_y;
	this->m_w=t_w;
	this->m_h=t_h;
	return this;
}
c_Panel* c_Panel::m_new2(){
	DBG_ENTER("Panel.new")
	c_Panel *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/panel.monkey<9>");
	c_ContainerGadget::m_new2();
	return this;
}
void c_Panel::mark(){
	c_ContainerGadget::mark();
}
String c_Panel::debug(){
	String t="(Panel)\n";
	t=c_ContainerGadget::debug()+t;
	t+=dbg_decl("yMax",&m_yMax);
	return t;
}
c_MapValues::c_MapValues(){
	m_map=0;
}
c_MapValues* c_MapValues::m_new(c_Map2* t_map){
	DBG_ENTER("MapValues.new")
	c_MapValues *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_map,"map")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<519>");
	gc_assign(this->m_map,t_map);
	return this;
}
c_MapValues* c_MapValues::m_new2(){
	DBG_ENTER("MapValues.new")
	c_MapValues *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<516>");
	return this;
}
c_ValueEnumerator* c_MapValues::p_ObjectEnumerator(){
	DBG_ENTER("MapValues.ObjectEnumerator")
	c_MapValues *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<523>");
	c_ValueEnumerator* t_=(new c_ValueEnumerator)->m_new(m_map->p_FirstNode());
	return t_;
}
void c_MapValues::mark(){
	Object::mark();
	gc_mark_q(m_map);
}
String c_MapValues::debug(){
	String t="(MapValues)\n";
	t+=dbg_decl("map",&m_map);
	return t;
}
c_ValueEnumerator::c_ValueEnumerator(){
	m_node=0;
}
c_ValueEnumerator* c_ValueEnumerator::m_new(c_Node3* t_node){
	DBG_ENTER("ValueEnumerator.new")
	c_ValueEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<481>");
	gc_assign(this->m_node,t_node);
	return this;
}
c_ValueEnumerator* c_ValueEnumerator::m_new2(){
	DBG_ENTER("ValueEnumerator.new")
	c_ValueEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<478>");
	return this;
}
bool c_ValueEnumerator::p_HasNext(){
	DBG_ENTER("ValueEnumerator.HasNext")
	c_ValueEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<485>");
	bool t_=m_node!=0;
	return t_;
}
c_Setting* c_ValueEnumerator::p_NextObject(){
	DBG_ENTER("ValueEnumerator.NextObject")
	c_ValueEnumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<489>");
	c_Node3* t_t=m_node;
	DBG_LOCAL(t_t,"t")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<490>");
	gc_assign(m_node,m_node->p_NextNode());
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/map.monkey<491>");
	return t_t->m_value;
}
void c_ValueEnumerator::mark(){
	Object::mark();
	gc_mark_q(m_node);
}
String c_ValueEnumerator::debug(){
	String t="(ValueEnumerator)\n";
	t+=dbg_decl("node",&m_node);
	return t;
}
c_Slider::c_Slider(){
	m_name=String();
	m_index=0;
}
c_Slider* c_Slider::m_new(int t_x,int t_y,String t_name,int t_index){
	DBG_ENTER("Slider.new")
	c_Slider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_index,"index")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<107>");
	c_Gadget::m_new();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<108>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<109>");
	this->m_w=62;
	this->m_h=16;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<110>");
	this->m_name=t_name;
	this->m_index=t_index;
	return this;
}
c_Slider* c_Slider::m_new2(){
	DBG_ENTER("Slider.new")
	c_Slider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<102>");
	c_Gadget::m_new();
	return this;
}
c_Gadget* c_Slider::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("Slider.HandleEvent")
	c_Slider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<114>");
	int t_2=t_event->m_id;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<115>");
	if(t_2==1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<116>");
		bb_main_sx=m_index;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<117>");
		if(t_2==3){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<118>");
			m_index=int(Float(bb_main_sx)+Float(t_event->m_dx)/FLOAT(3.0));
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<119>");
			m_index=bb_math_Max(0,m_index);
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<120>");
			m_index=bb_math_Min(18,m_index);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<121>");
			if(t_2==5){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<122>");
				m_parent->p_HandleGadgetEvent((new c_GadgetEvent)->m_new(this));
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<126>");
	c_Gadget* t_=(this);
	return t_;
}
void c_Slider::p_OnRender(){
	DBG_ENTER("Slider.OnRender")
	c_Slider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<130>");
	bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<131>");
	bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<132>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<133>");
	bb_graphics_DrawRect(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<134>");
	bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<135>");
	bb_graphics_DrawRect(Float(3*m_index),FLOAT(1.0),FLOAT(7.0),Float(m_h-2));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<136>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<137>");
	bb_graphics_DrawText(m_name,FLOAT(2.0),FLOAT(1.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<139>");
	bb_graphics_DrawText(String(L"%",1)+String(5*(m_index+1)),Float(m_w+4),FLOAT(2.0),FLOAT(0.0),FLOAT(0.0));
}
void c_Slider::mark(){
	c_Gadget::mark();
}
String c_Slider::debug(){
	String t="(Slider)\n";
	t=c_Gadget::debug()+t;
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("index",&m_index);
	return t;
}
c_NumberBox::c_NumberBox(){
	m_name=String();
	m_value=0;
	m_minimum=0;
	m_maximum=0;
	m_downButton=0;
	m_upButton=0;
}
c_NumberBox* c_NumberBox::m_new(int t_x,int t_y,String t_name,int t_value,int t_minimum,int t_maximum){
	DBG_ENTER("NumberBox.new")
	c_NumberBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_minimum,"minimum")
	DBG_LOCAL(t_maximum,"maximum")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<191>");
	c_ContainerGadget::m_new2();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<192>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<193>");
	this->m_w=int(FLOAT(18.0)+bb_graphics_TextWidth(String(L"00",2))+FLOAT(2.0)+FLOAT(16.0));
	this->m_h=16;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<194>");
	this->m_name=t_name;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<195>");
	this->m_value=t_value;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<196>");
	this->m_minimum=t_minimum;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<197>");
	this->m_maximum=t_maximum;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<198>");
	gc_assign(m_downButton,(new c_ButtonGadget)->m_new(0,0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<199>");
	gc_assign(m_upButton,(new c_ButtonGadget)->m_new(int(FLOAT(18.0)+bb_graphics_TextWidth(String(L"00",2))+FLOAT(2.0)),0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<200>");
	p_AddChild(m_downButton);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<201>");
	p_AddChild(m_upButton);
	return this;
}
c_NumberBox* c_NumberBox::m_new2(){
	DBG_ENTER("NumberBox.new")
	c_NumberBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<185>");
	c_ContainerGadget::m_new2();
	return this;
}
void c_NumberBox::p_HandleGadgetEvent(c_GadgetEvent* t_event){
	DBG_ENTER("NumberBox.HandleGadgetEvent")
	c_NumberBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<205>");
	if(t_event->m_source==(m_downButton)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<206>");
		m_value=bb_math_Max(m_value-1,m_minimum);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<207>");
		if(t_event->m_source==(m_upButton)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<208>");
			m_value=bb_math_Min(m_value+1,m_maximum);
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<211>");
	m_parent->p_HandleGadgetEvent((new c_GadgetEvent)->m_new(this));
}
void c_NumberBox::p_OnRenderInterior(){
	DBG_ENTER("NumberBox.OnRenderInterior")
	c_NumberBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<215>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<216>");
	c_ContainerGadget::p_OnRenderInterior();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<217>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<218>");
	bb_graphics_DrawText(String(m_value),FLOAT(18.0),FLOAT(2.0),FLOAT(0.0),FLOAT(0.0));
}
void c_NumberBox::mark(){
	c_ContainerGadget::mark();
	gc_mark_q(m_downButton);
	gc_mark_q(m_upButton);
}
String c_NumberBox::debug(){
	String t="(NumberBox)\n";
	t=c_ContainerGadget::debug()+t;
	t+=dbg_decl("downButton",&m_downButton);
	t+=dbg_decl("upButton",&m_upButton);
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("minimum",&m_minimum);
	t+=dbg_decl("maximum",&m_maximum);
	return t;
}
Float bb_graphics_TextWidth(String t_text){
	DBG_ENTER("TextWidth")
	DBG_LOCAL(t_text,"text")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<563>");
	if((bb_graphics_context->m_font)!=0){
		DBG_BLOCK();
		Float t_=Float(t_text.Length()*bb_graphics_context->m_font->p_Width());
		return t_;
	}
	return 0;
}
c_DropList::c_DropList(){
	m_name=String();
	m_values=Array<String >();
	m_index=0;
	m_mode=0;
}
c_DropList* c_DropList::m_new(int t_x,int t_y,String t_name,Array<String > t_values,int t_index){
	DBG_ENTER("DropList.new")
	c_DropList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_index,"index")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<230>");
	c_Gadget::m_new();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<231>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<232>");
	m_w=64;
	m_h=16;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<233>");
	this->m_name=t_name;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<234>");
	gc_assign(this->m_values,t_values);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<235>");
	this->m_index=t_index;
	return this;
}
c_DropList* c_DropList::m_new2(){
	DBG_ENTER("DropList.new")
	c_DropList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<224>");
	c_Gadget::m_new();
	return this;
}
c_Gadget* c_DropList::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("DropList.HandleEvent")
	c_DropList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<239>");
	int t_4=t_event->m_id;
	DBG_LOCAL(t_4,"4")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<240>");
	if(t_4==1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<241>");
		if(m_mode==0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<242>");
			m_mode=1;
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<243>");
			m_h=m_values.Length()*16+16;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<245>");
			m_mode=0;
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<246>");
			m_h=16;
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<247>");
			int t_n=t_event->m_y/16;
			DBG_LOCAL(t_n,"n")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<249>");
			if(t_n!=0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<250>");
				m_index=t_n-1;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<251>");
				m_parent->p_HandleGadgetEvent((new c_GadgetEvent)->m_new(this));
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<257>");
	c_Gadget* t_=(this);
	return t_;
}
void c_DropList::p_OnRender(){
	DBG_ENTER("DropList.OnRender")
	c_DropList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<261>");
	bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<262>");
	bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<263>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<264>");
	bb_graphics_DrawRect(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<265>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<266>");
	bb_graphics_DrawText(m_values.At(m_index),FLOAT(2.0),FLOAT(2.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<268>");
	if(m_mode==1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<269>");
		for(int t_n=0;t_n<m_values.Length();t_n=t_n+1){
			DBG_BLOCK();
			DBG_LOCAL(t_n,"n")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<270>");
			bb_graphics_DrawText(m_values.At(t_n),FLOAT(2.0),Float(2+16*t_n+16),FLOAT(0.0),FLOAT(0.0));
		}
	}
}
void c_DropList::mark(){
	c_Gadget::mark();
	gc_mark_q(m_values);
}
String c_DropList::debug(){
	String t="(DropList)\n";
	t=c_Gadget::debug()+t;
	t+=dbg_decl("values",&m_values);
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("index",&m_index);
	t+=dbg_decl("mode",&m_mode);
	return t;
}
c_CheckBox::c_CheckBox(){
	m_name=String();
	m_on=0;
	m_state=0;
}
c_CheckBox* c_CheckBox::m_new(int t_x,int t_y,String t_name,int t_on){
	DBG_ENTER("CheckBox.new")
	c_CheckBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_on,"on")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<13>");
	c_Gadget::m_new();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<14>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<15>");
	m_w=16;
	m_h=16;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<16>");
	this->m_name=t_name;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<17>");
	this->m_on=t_on;
	return this;
}
c_CheckBox* c_CheckBox::m_new2(){
	DBG_ENTER("CheckBox.new")
	c_CheckBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<8>");
	c_Gadget::m_new();
	return this;
}
c_Gadget* c_CheckBox::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("CheckBox.HandleEvent")
	c_CheckBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<21>");
	int t_1=t_event->m_id;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<27>");
	if(t_1==1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<28>");
		m_state=2;
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<29>");
		if(t_1==5){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<30>");
			m_state=0;
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<31>");
			c_Gadget* t_gadget=bb_main_APP->m_window->p_HandleEvent((new c_Event)->m_new(-1,c_Event::m_globalWindow));
			DBG_LOCAL(t_gadget,"gadget")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<33>");
			if(t_gadget==(this)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<34>");
				m_on=1-m_on;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<35>");
				m_parent->p_HandleGadgetEvent((new c_GadgetEvent)->m_new(this));
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<40>");
	c_Gadget* t_=(this);
	return t_;
}
void c_CheckBox::p_OnRender(){
	DBG_ENTER("CheckBox.OnRender")
	c_CheckBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<44>");
	bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<59>");
	bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<60>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<61>");
	bb_graphics_DrawRect(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<63>");
	if(m_on==1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<64>");
		bb_graphics_SetColor(FLOAT(112.0),FLOAT(146.0),FLOAT(190.0));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<65>");
		bb_graphics_DrawLine(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<66>");
		bb_graphics_DrawLine(FLOAT(1.0),Float(m_h-2),Float(m_w-2),FLOAT(1.0));
	}
}
void c_CheckBox::mark(){
	c_Gadget::mark();
}
String c_CheckBox::debug(){
	String t="(CheckBox)\n";
	t=c_Gadget::debug()+t;
	t+=dbg_decl("state",&m_state);
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("on",&m_on);
	return t;
}
c_RuleTable::c_RuleTable(){
	m_rules=0;
	m_name=String();
	m_checkBoxes=Array<c_CheckBox* >();
}
c_RuleTable* c_RuleTable::m_new(int t_x,int t_y,String t_name,int t_rulesId){
	DBG_ENTER("RuleTable.new")
	c_RuleTable *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_rulesId,"rulesId")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<304>");
	c_ContainerGadget::m_new2();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<305>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<306>");
	m_w=48;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<307>");
	c_Rules* t_rules=bb_gadgets_ruleTables->p_Get2(t_rulesId);
	DBG_LOCAL(t_rules,"rules")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<309>");
	if(t_rules==0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<310>");
		int t_[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
		t_rules=(new c_Rules)->m_new(t_rulesId,Array<int >(t_,18));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<311>");
		bb_gadgets_ruleTables->p_Insert3(t_rulesId,t_rules);
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<314>");
	gc_assign(this->m_rules,t_rules);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<315>");
	m_h=t_rules->m_value.Length()*16;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<316>");
	this->m_name=t_name;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<317>");
	gc_assign(m_checkBoxes,m_checkBoxes.Resize(t_rules->m_value.Length()));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<319>");
	for(int t_n=0;t_n<m_checkBoxes.Length();t_n=t_n+1){
		DBG_BLOCK();
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<320>");
		c_CheckBox* t_checkBox=(new c_CheckBox)->m_new(32,t_n*16,String(),t_rules->m_value.At(t_n));
		DBG_LOCAL(t_checkBox,"checkBox")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<321>");
		gc_assign(m_checkBoxes.At(t_n),t_checkBox);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<322>");
		p_AddChild(t_checkBox);
	}
	return this;
}
c_RuleTable* c_RuleTable::m_new2(){
	DBG_ENTER("RuleTable.new")
	c_RuleTable *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<298>");
	c_ContainerGadget::m_new2();
	return this;
}
void c_RuleTable::p_HandleGadgetEvent(c_GadgetEvent* t_event){
	DBG_ENTER("RuleTable.HandleGadgetEvent")
	c_RuleTable *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<327>");
	for(int t_n=0;t_n<m_checkBoxes.Length();t_n=t_n+1){
		DBG_BLOCK();
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<328>");
		if(t_event->m_source==(m_checkBoxes.At(t_n))){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<329>");
			m_rules->m_value.At(t_n)=m_checkBoxes.At(t_n)->m_on;
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<333>");
	m_parent->p_HandleGadgetEvent((new c_GadgetEvent)->m_new(this));
}
void c_RuleTable::p_OnRender(){
	DBG_ENTER("RuleTable.OnRender")
	c_RuleTable *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<337>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<339>");
	for(int t_n=0;t_n<m_rules->m_value.Length();t_n=t_n+1){
		DBG_BLOCK();
		DBG_LOCAL(t_n,"n")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<340>");
		int t_b=t_n % 9;
		DBG_LOCAL(t_b,"b")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<341>");
		int t_a=(t_n-t_b)/9;
		DBG_LOCAL(t_a,"a")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<342>");
		bb_graphics_DrawText(String(t_a),FLOAT(4.0),Float(t_n*16),FLOAT(0.0),FLOAT(0.0));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<343>");
		bb_graphics_DrawText(String(t_b),FLOAT(20.0),Float(t_n*16),FLOAT(0.0),FLOAT(0.0));
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<344>");
		bb_graphics_DrawText(String(m_rules->m_value.At(t_n)),FLOAT(36.0),Float(t_n*16),FLOAT(0.0),FLOAT(0.0));
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gadgets.monkey<347>");
	c_ContainerGadget::p_OnRender();
}
void c_RuleTable::mark(){
	c_ContainerGadget::mark();
	gc_mark_q(m_rules);
	gc_mark_q(m_checkBoxes);
}
String c_RuleTable::debug(){
	String t="(RuleTable)\n";
	t=c_ContainerGadget::debug()+t;
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("rules",&m_rules);
	t+=dbg_decl("checkBoxes",&m_checkBoxes);
	return t;
}
void bb_patch_SelectBox(c_Box* t_box){
	DBG_ENTER("SelectBox")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<190>");
	gc_assign(bb_main_APP->p_project()->m_boxSelected,t_box);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<191>");
	bb_main_APP->p_panel()->m_children->p_Clear();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<192>");
	if(t_box==0){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<193>");
	int t_y=4;
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<195>");
	c_ValueEnumerator* t_=t_box->m_settings->p_Values()->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Setting* t_setting=t_->p_NextObject();
		DBG_LOCAL(t_setting,"setting")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<196>");
		String t_4=t_setting->m_kind;
		DBG_LOCAL(t_4,"4")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<197>");
		if(t_4==String(L"f",1)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<198>");
			bb_main_APP->p_panel()->p_AddChild((new c_Slider)->m_new(4,t_y,t_setting->m_name,t_setting->m_value));
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<199>");
			if(t_4==String(L"i1-9",4)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<200>");
				bb_main_APP->p_panel()->p_AddChild((new c_NumberBox)->m_new(4,t_y,t_setting->m_name,t_setting->m_value,1,9));
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<201>");
				if(t_4==String(L"dedge",5)){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<202>");
					String t_2[]={String(L"dead",4),String(L"alive",5),String(L"wrap",4)};
					bb_main_APP->p_panel()->p_AddChild((new c_DropList)->m_new(4,t_y,t_setting->m_name,Array<String >(t_2,3),t_setting->m_value));
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<203>");
					if(t_4==String(L"b",1)){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<204>");
						bb_main_APP->p_panel()->p_AddChild((new c_CheckBox)->m_new(4,t_y,t_setting->m_name,t_setting->m_value));
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<205>");
						if(t_4==String(L"a9s8",4)){
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<206>");
							bb_main_APP->p_panel()->p_AddChild((new c_RuleTable)->m_new(4,t_y,t_setting->m_name,t_setting->m_value));
						}
					}
				}
			}
		}
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/patch.monkey<211>");
		t_y=t_y+16+4;
	}
}
bool bb_main_CycleCheck(c_Box* t_a,c_Box* t_b){
	DBG_ENTER("CycleCheck")
	DBG_LOCAL(t_a,"a")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<190>");
	c_List6* t_list=(new c_List6)->m_new();
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<191>");
	t_list->p_AddLast6(t_b);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<192>");
	int t_count=0;
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<194>");
	while(t_list->p_Count()!=t_count){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<195>");
		t_count=t_list->p_Count();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<197>");
		c_Enumerator4* t_=bb_main_APP->p_patch()->m_wires->p_ObjectEnumerator();
		while(t_->p_HasNext()){
			DBG_BLOCK();
			c_Wire* t_wire=t_->p_NextObject();
			DBG_LOCAL(t_wire,"wire")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<198>");
			if(t_list->p_Contains2(t_wire->m_b)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<199>");
				if(!t_list->p_Contains2(t_wire->m_a)){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<200>");
					t_list->p_AddLast6(t_wire->m_a);
				}
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<206>");
	if(t_list->p_Contains2(t_a)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<207>");
		return true;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<210>");
	return false;
}
void bb_main_DeleteBox(c_Box* t_box){
	DBG_ENTER("DeleteBox")
	DBG_LOCAL(t_box,"box")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<162>");
	if(t_box==0){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<166>");
	c_Enumerator4* t_=bb_main_APP->p_patch()->m_wires->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Wire* t_wire=t_->p_NextObject();
		DBG_LOCAL(t_wire,"wire")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<167>");
		if(t_wire->m_a==t_box || t_wire->m_b==t_box){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<168>");
			bb_main_DeleteWire(t_wire);
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/main.monkey<172>");
	bb_main_APP->p_patch()->m_boxes->p_Remove5(t_box);
}
c_ViewBox::c_ViewBox(){
}
void c_ViewBox::p_Render(){
	DBG_ENTER("ViewBox.Render")
	c_ViewBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<111>");
	c_Box::p_Render();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<112>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<114>");
	for(int t__x=0;t__x<20;t__x=t__x+1){
		DBG_BLOCK();
		DBG_LOCAL(t__x,"_x")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<115>");
		for(int t__y=0;t__y<15;t__y=t__y+1){
			DBG_BLOCK();
			DBG_LOCAL(t__y,"_y")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<116>");
			if(m_state.At(t__x).At(t__y)==1){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<117>");
				bb_graphics_DrawRect(Float(2+m_x+t__x*4),Float(2+m_y+t__y*4),FLOAT(4.0),FLOAT(4.0));
			}
		}
	}
}
c_ViewBox* c_ViewBox::m_new(int t_x,int t_y){
	DBG_ENTER("ViewBox.new")
	c_ViewBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<101>");
	c_Box::m_new2();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<102>");
	this->m_x=t_x;
	this->m_y=t_y;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<103>");
	m_w=84;
	m_h=64;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<104>");
	m_kind=String(L"view",4);
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<105>");
	m_ins=1;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<106>");
	m_outs=0;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<107>");
	gc_assign(m_state,bb_box_Initialize2dArray(20,15));
	return this;
}
c_ViewBox* c_ViewBox::m_new2(){
	DBG_ENTER("ViewBox.new")
	c_ViewBox *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/box.monkey<100>");
	c_Box::m_new2();
	return this;
}
void c_ViewBox::mark(){
	c_Box::mark();
}
String c_ViewBox::debug(){
	String t="(ViewBox)\n";
	t=c_Box::debug()+t;
	return t;
}
c_List9::c_List9(){
	m__head=((new c_HeadNode9)->m_new());
}
c_List9* c_List9::m_new(){
	DBG_ENTER("List.new")
	c_List9 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node12* c_List9::p_AddLast9(String t_data){
	DBG_ENTER("List.AddLast")
	c_List9 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<108>");
	c_Node12* t_=(new c_Node12)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List9* c_List9::m_new2(Array<String > t_data){
	DBG_ENTER("List.new")
	c_List9 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<13>");
	Array<String > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<14>");
		p_AddLast9(t_t);
	}
	return this;
}
bool c_List9::p_Equals8(String t_lhs,String t_rhs){
	DBG_ENTER("List.Equals")
	c_List9 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
bool c_List9::p_Contains3(String t_value){
	DBG_ENTER("List.Contains")
	c_List9 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<54>");
	c_Node12* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<55>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<56>");
		if(p_Equals8(t_node->m__data,t_value)){
			DBG_BLOCK();
			return true;
		}
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<57>");
		t_node=t_node->m__succ;
	}
	return false;
}
void c_List9::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List9::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node12::c_Node12(){
	m__succ=0;
	m__pred=0;
	m__data=String();
}
c_Node12* c_Node12::m_new(c_Node12* t_succ,c_Node12* t_pred,String t_data){
	DBG_ENTER("Node.new")
	c_Node12 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<265>");
	m__data=t_data;
	return this;
}
c_Node12* c_Node12::m_new2(){
	DBG_ENTER("Node.new")
	c_Node12 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<258>");
	return this;
}
void c_Node12::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
}
String c_Node12::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode9::c_HeadNode9(){
}
c_HeadNode9* c_HeadNode9::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode9 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<310>");
	c_Node12::m_new2();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode9::mark(){
	c_Node12::mark();
}
String c_HeadNode9::debug(){
	String t="(HeadNode)\n";
	t=c_Node12::debug()+t;
	return t;
}
c_List9* bb_functions_implementedTemplates;
int bb_graphics_DrawText(String t_text,Float t_x,Float t_y,Float t_xalign,Float t_yalign){
	DBG_ENTER("DrawText")
	DBG_LOCAL(t_text,"text")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_xalign,"xalign")
	DBG_LOCAL(t_yalign,"yalign")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<576>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<578>");
	if(!((bb_graphics_context->m_font)!=0)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<580>");
	int t_w=bb_graphics_context->m_font->p_Width();
	DBG_LOCAL(t_w,"w")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<581>");
	int t_h=bb_graphics_context->m_font->p_Height();
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<583>");
	t_x-=(Float)floor(Float(t_w*t_text.Length())*t_xalign);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<584>");
	t_y-=(Float)floor(Float(t_h)*t_yalign);
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<586>");
	for(int t_i=0;t_i<t_text.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<587>");
		int t_ch=(int)t_text.At(t_i)-bb_graphics_context->m_firstChar;
		DBG_LOCAL(t_ch,"ch")
		DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<588>");
		if(t_ch>=0 && t_ch<bb_graphics_context->m_font->p_Frames()){
			DBG_BLOCK();
			DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<589>");
			bb_graphics_DrawImage(bb_graphics_context->m_font,t_x+Float(t_i*t_w),t_y,t_ch);
		}
	}
	return 0;
}
int bb_graphics_DrawOval(Float t_x,Float t_y,Float t_w,Float t_h){
	DBG_ENTER("DrawOval")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<409>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<411>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/mojo/graphics.monkey<412>");
	bb_graphics_renderDevice->DrawOval(t_x,t_y,t_w,t_h);
	return 0;
}
String bb_os_StripExt(String t_path){
	DBG_ENTER("StripExt")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/os/os.monkey<147>");
	int t_i=t_path.FindLast(String(L".",1));
	DBG_LOCAL(t_i,"i")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/os/os.monkey<148>");
	if(t_i!=-1 && t_path.Find(String(L"/",1),t_i+1)==-1 && t_path.Find(String(L"\\",1),t_i+1)==-1){
		DBG_BLOCK();
		String t_=t_path.Slice(0,t_i);
		return t_;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/os/os.monkey<149>");
	return t_path;
}
String bb_os_StripDir(String t_path){
	DBG_ENTER("StripDir")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/os/os.monkey<134>");
	int t_i=t_path.FindLast(String(L"/",1));
	DBG_LOCAL(t_i,"i")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/os/os.monkey<135>");
	if(t_i==-1){
		DBG_BLOCK();
		t_i=t_path.FindLast(String(L"\\",1));
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/os/os.monkey<136>");
	if(t_i!=-1){
		DBG_BLOCK();
		String t_=t_path.Slice(t_i+1);
		return t_;
	}
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/os/os.monkey<137>");
	return t_path;
}
String bb_os_StripAll(String t_path){
	DBG_ENTER("StripAll")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/Monkey/MonkeyXPro79e/modules/os/os.monkey<159>");
	String t_=bb_os_StripDir(bb_os_StripExt(t_path));
	return t_;
}
c_Tray::c_Tray(){
	m_boxes=(new c_List6)->m_new();
	m_xMax=0;
	m_boxOver=0;
	m_boxSelected=0;
}
c_Tray* c_Tray::m_new(int t_x,int t_y,int t_w,int t_h){
	DBG_ENTER("Tray.new")
	c_Tray *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<16>");
	c_ViewGadget::m_new();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<17>");
	this->m_x=t_x;
	this->m_y=t_y;
	this->m_w=t_w;
	this->m_h=t_h;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<18>");
	t_x=10;
	t_y=5;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<20>");
	c_Enumerator* t_=bb_template_templates->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Template* t_template=t_->p_NextObject();
		DBG_LOCAL(t_template,"template")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<21>");
		c_Box* t_box=(new c_Box)->m_new(t_x,t_y,t_template);
		DBG_LOCAL(t_box,"box")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<22>");
		m_boxes->p_AddLast6(t_box);
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<23>");
		t_x=t_x+t_box->m_w+10;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<26>");
	m_xMax=bb_math_Max(t_w,t_x)-t_w;
	return this;
}
c_Tray* c_Tray::m_new2(){
	DBG_ENTER("Tray.new")
	c_Tray *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<9>");
	c_ViewGadget::m_new();
	return this;
}
void c_Tray::p_UpdateHover2(c_Event* t_event){
	DBG_ENTER("Tray.UpdateHover")
	c_Tray *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<85>");
	m_boxOver=0;
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<87>");
	c_Enumerator5* t_=m_boxes->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Box* t_box=t_->p_NextObject();
		DBG_LOCAL(t_box,"box")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<88>");
		if((bb_gui_RectangleContainsPoint(t_box->m_x,t_box->m_y,t_box->m_w,t_box->m_h,t_event->m_x,t_event->m_y))!=0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<89>");
			gc_assign(m_boxOver,t_box);
		}
	}
}
c_Gadget* c_Tray::p_HandleEvent(c_Event* t_event){
	DBG_ENTER("Tray.HandleEvent")
	c_Tray *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<30>");
	int t_1=t_event->m_id;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<31>");
	if(t_1==0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<32>");
		p_UpdateHover2(t_event);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<33>");
		if(t_1==3){
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<34>");
			int t_2=bb_main__dragMode;
			DBG_LOCAL(t_2,"2")
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<35>");
			if(t_2==1){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<36>");
				m_boxSelected->m_x=bb_main_sx+t_event->m_dx;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<37>");
				m_boxSelected->m_y=bb_main_sy+t_event->m_dy;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<38>");
				if(t_2==2){
					DBG_BLOCK();
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<42>");
			if(t_1==4){
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<43>");
				m_ox=bb_main_sx+t_event->m_dx;
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<44>");
				m_ox=-bb_math_Max(0,bb_math_Min(-m_ox,m_xMax));
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<45>");
				if(t_1==1){
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<46>");
					if(m_boxOver!=0){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<47>");
						bb_main__dragMode=1;
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<48>");
						gc_assign(m_boxSelected,m_boxOver);
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<49>");
						m_boxes->p_Remove5(m_boxSelected);
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<50>");
						m_boxes->p_AddLast6(m_boxSelected);
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<51>");
						bb_main_sx=m_boxSelected->m_x;
						bb_main_sy=m_boxSelected->m_y;
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<53>");
						m_boxSelected=0;
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<55>");
					if(t_1==2){
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<56>");
						bb_main_sx=m_ox;
						bb_main_sy=m_oy;
					}else{
						DBG_BLOCK();
						DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<57>");
						if(t_1==5){
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<58>");
							if(bb_main__dragMode==1){
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<59>");
								m_boxSelected->m_x=bb_main_sx;
								m_boxSelected->m_y=bb_main_sy;
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<60>");
								c_Gadget* t_gadget=m_window->p_HandleEvent((new c_Event)->m_new(-1,c_Event::m_globalWindow));
								DBG_LOCAL(t_gadget,"gadget")
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<62>");
								if(t_gadget==(bb_main_APP->p_patch())){
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<63>");
									c_Box* t_box=(new c_Box)->m_new(bb_main_APP->p_patch()->p_GetLocalX(bb_main_sx+t_event->m_dx,(this)),bb_main_APP->p_patch()->p_GetLocalY(bb_main_sy+t_event->m_dy,(this)),bb_template__GetTemplate(m_boxSelected->m_kind));
									DBG_LOCAL(t_box,"box")
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<65>");
									if(m_boxSelected->m_kind==String(L"view",4)){
										DBG_BLOCK();
										DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<66>");
										t_box=((new c_ViewBox)->m_new(bb_main_APP->p_patch()->p_GetLocalX(bb_main_sx+t_event->m_dx,(this)),bb_main_APP->p_patch()->p_GetLocalY(bb_main_sy+t_event->m_dy,(this))));
									}
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<69>");
									bb_main_APP->p_patch()->m_boxes->p_AddLast6(t_box);
								}
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<72>");
								m_boxSelected=0;
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<73>");
								bb_main__dragMode=0;
							}
						}else{
							DBG_BLOCK();
							DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<75>");
							if(t_1==7){
								DBG_BLOCK();
								DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<76>");
								if(m_boxOver!=0 && m_boxOver->p_isClickable() && m_boxOver->m_outs==0){
									DBG_BLOCK();
									DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<77>");
									m_boxOver->p_Execute();
								}
							}
						}
					}
				}
			}
		}
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<81>");
	c_Gadget* t_=(this);
	return t_;
}
void c_Tray::p_OnRender(){
	DBG_ENTER("Tray.OnRender")
	c_Tray *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<95>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<96>");
	bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(m_w),Float(m_h));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<97>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<98>");
	bb_graphics_DrawRect(FLOAT(1.0),FLOAT(1.0),Float(m_w-2),Float(m_h-2));
}
void c_Tray::p_OnRenderInterior(){
	DBG_ENTER("Tray.OnRenderInterior")
	c_Tray *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<102>");
	c_Enumerator5* t_=m_boxes->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Box* t_box=t_->p_NextObject();
		DBG_LOCAL(t_box,"box")
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<103>");
		t_box->p_Render();
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<106>");
	if(m_boxSelected!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<107>");
		bb_gui_DisableScissor();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<108>");
		m_boxSelected->p_Render();
		DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/tray.monkey<109>");
		bb_gui_EnableScissor();
	}
}
void c_Tray::mark(){
	c_ViewGadget::mark();
	gc_mark_q(m_boxes);
	gc_mark_q(m_boxOver);
	gc_mark_q(m_boxSelected);
}
String c_Tray::debug(){
	String t="(Tray)\n";
	t=c_ViewGadget::debug()+t;
	t+=dbg_decl("boxes",&m_boxes);
	t+=dbg_decl("xMax",&m_xMax);
	t+=dbg_decl("boxOver",&m_boxOver);
	t+=dbg_decl("boxSelected",&m_boxSelected);
	return t;
}
void bb_gui_DisableScissor(){
	DBG_ENTER("DisableScissor")
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<52>");
	if(!bb_gui__scissorEnabled){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<53>");
	bb_gui_PushScissor();
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<54>");
	bb_graphics_SetScissor(FLOAT(0.0),FLOAT(0.0),FLOAT(640.0),FLOAT(480.0));
	DBG_INFO("C:/Users/Zachary/Documents/GitHub/clay/gui/gui.monkey<55>");
	bb_gui__scissorEnabled=false;
}
int bbInit(){
	GC_CTOR
	bb_app__app=0;
	DBG_GLOBAL("_app",&bb_app__app);
	bb_app__delegate=0;
	DBG_GLOBAL("_delegate",&bb_app__delegate);
	bb_app__game=BBGame::Game();
	DBG_GLOBAL("_game",&bb_app__game);
	bb_main_APP=0;
	DBG_GLOBAL("APP",&bb_main_APP);
	bb_graphics_device=0;
	DBG_GLOBAL("device",&bb_graphics_device);
	bb_graphics_context=(new c_GraphicsContext)->m_new();
	DBG_GLOBAL("context",&bb_graphics_context);
	c_Image::m_DefaultFlags=0;
	DBG_GLOBAL("DefaultFlags",&c_Image::m_DefaultFlags);
	bb_audio_device=0;
	DBG_GLOBAL("device",&bb_audio_device);
	bb_input_device=0;
	DBG_GLOBAL("device",&bb_input_device);
	bb_app__devWidth=0;
	DBG_GLOBAL("_devWidth",&bb_app__devWidth);
	bb_app__devHeight=0;
	DBG_GLOBAL("_devHeight",&bb_app__devHeight);
	bb_app__displayModes=Array<c_DisplayMode* >();
	DBG_GLOBAL("_displayModes",&bb_app__displayModes);
	bb_app__desktopMode=0;
	DBG_GLOBAL("_desktopMode",&bb_app__desktopMode);
	bb_graphics_renderDevice=0;
	DBG_GLOBAL("renderDevice",&bb_graphics_renderDevice);
	bb_app__updateRate=0;
	DBG_GLOBAL("_updateRate",&bb_app__updateRate);
	bb_main_imgX=0;
	DBG_GLOBAL("imgX",&bb_main_imgX);
	bb_main_imgO=0;
	DBG_GLOBAL("imgO",&bb_main_imgO);
	bb_main_imgTab=0;
	DBG_GLOBAL("imgTab",&bb_main_imgTab);
	bb_main_imgClose=0;
	DBG_GLOBAL("imgClose",&bb_main_imgClose);
	bb_main_imgOpen=0;
	DBG_GLOBAL("imgOpen",&bb_main_imgOpen);
	bb_main_imgSave=0;
	DBG_GLOBAL("imgSave",&bb_main_imgSave);
	bb_main_imgNew=0;
	DBG_GLOBAL("imgNew",&bb_main_imgNew);
	bb_template_templates=(new c_List)->m_new();
	DBG_GLOBAL("templates",&bb_template_templates);
	c_Event::m_globalWindow=0;
	DBG_GLOBAL("globalWindow",&c_Event::m_globalWindow);
	bb_event_MOUSE_STATE_NONE=0;
	DBG_GLOBAL("MOUSE_STATE_NONE",&bb_event_MOUSE_STATE_NONE);
	bb_event_MOUSE_STATE_RIGHT=2;
	DBG_GLOBAL("MOUSE_STATE_RIGHT",&bb_event_MOUSE_STATE_RIGHT);
	bb_event_MOUSE_STATE_BOTH_LEFT=3;
	DBG_GLOBAL("MOUSE_STATE_BOTH_LEFT",&bb_event_MOUSE_STATE_BOTH_LEFT);
	bb_event_MOUSE_STATE_BOTH_RIGHT=4;
	DBG_GLOBAL("MOUSE_STATE_BOTH_RIGHT",&bb_event_MOUSE_STATE_BOTH_RIGHT);
	bb_event_MOUSE_STATE_LEFT=1;
	DBG_GLOBAL("MOUSE_STATE_LEFT",&bb_event_MOUSE_STATE_LEFT);
	bb_random_Seed=1234;
	DBG_GLOBAL("Seed",&bb_random_Seed);
	bb_gadgets_ruleTables=(new c_IntMap2)->m_new();
	DBG_GLOBAL("ruleTables",&bb_gadgets_ruleTables);
	bb_gui__scissorEnabled=true;
	DBG_GLOBAL("_scissorEnabled",&bb_gui__scissorEnabled);
	bb_gui__scissors=(new c_List7)->m_new();
	DBG_GLOBAL("_scissors",&bb_gui__scissors);
	bb_main__dragMode=0;
	DBG_GLOBAL("_dragMode",&bb_main__dragMode);
	bb_main_sx=0;
	DBG_GLOBAL("sx",&bb_main_sx);
	bb_main_sy=0;
	DBG_GLOBAL("sy",&bb_main_sy);
	bb_main_from=0;
	DBG_GLOBAL("from",&bb_main_from);
	String t_[]={String(L"go",2),String(L"clear",5),String(L"noise",5),String(L"automata",8),String(L"smooth",6),String(L"expand",6),String(L"contract",8),String(L"darken",6),String(L"lighten",7),String(L"invert",6),String(L"fill",4),String(L"view",4)};
	bb_functions_implementedTemplates=(new c_List9)->m_new2(Array<String >(t_,12));
	DBG_GLOBAL("implementedTemplates",&bb_functions_implementedTemplates);
	c_Box::m_idNext=0;
	DBG_GLOBAL("idNext",&c_Box::m_idNext);
	return 0;
}
void gc_mark(){
	gc_mark_q(bb_app__app);
	gc_mark_q(bb_app__delegate);
	gc_mark_q(bb_main_APP);
	gc_mark_q(bb_graphics_device);
	gc_mark_q(bb_graphics_context);
	gc_mark_q(bb_audio_device);
	gc_mark_q(bb_input_device);
	gc_mark_q(bb_app__displayModes);
	gc_mark_q(bb_app__desktopMode);
	gc_mark_q(bb_graphics_renderDevice);
	gc_mark_q(bb_main_imgX);
	gc_mark_q(bb_main_imgO);
	gc_mark_q(bb_main_imgTab);
	gc_mark_q(bb_main_imgClose);
	gc_mark_q(bb_main_imgOpen);
	gc_mark_q(bb_main_imgSave);
	gc_mark_q(bb_main_imgNew);
	gc_mark_q(bb_template_templates);
	gc_mark_q(c_Event::m_globalWindow);
	gc_mark_q(bb_gadgets_ruleTables);
	gc_mark_q(bb_gui__scissors);
	gc_mark_q(bb_main_from);
	gc_mark_q(bb_functions_implementedTemplates);
}
//${TRANSCODE_END}

int main( int argc,const char *argv[] ){

	BBMonkeyGame::Main( argc,argv );
}
