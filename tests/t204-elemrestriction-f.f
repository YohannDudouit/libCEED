c-----------------------------------------------------------------------
      program test

      include 'ceedf.h'

      integer ceed,err
      integer x,y
      integer r
      integer i,n
      integer*8 offset

      integer ne
      parameter(ne=3)

      integer*4 ind(2*ne)
      real*8 a(2*(ne+1))
      real*8 yy(4*ne)
      real*8 diff

      character arg*32

      call getarg(1,arg)
      call ceedinit(trim(arg)//char(0),ceed,err)

      call ceedvectorcreate(ceed,2*(ne+1),x,err)

      do i=1,ne+1
        a(i)=10+i-1
        a(i+ne+1)=20+i-1
      enddo

      call ceedvectorsetarray(x,ceed_mem_host,ceed_use_pointer,a,err)

      do i=1,ne
        ind(2*i-1)=i-1
        ind(2*i  )=i
      enddo

      call ceedelemrestrictioncreate(ceed,ne,2,ne+1,2,ceed_mem_host,
     $  ceed_use_pointer,ind,r,err)

      call ceedvectorcreate(ceed,2*(2*ne),y,err);
      call ceedvectorsetvalue(y,0.d0,err);
      call ceedelemrestrictionapply(r,ceed_notranspose,
     $  ceed_notranspose,x,y,ceed_request_immediate,err)

      call ceedvectorgetarrayread(y,ceed_mem_host,yy,offset,err)
      do i=0,ne-1
        do n=1,2
          diff=10+(2*i+n)/2-yy(i*4+n+offset)
          if (abs(diff) > 1.0D-15) then
            write(*,*) 'Error in restricted array y(',i*4+n,')=',
     $  yy(i*4+n+offset),'!=',10+(2*i+n)/2
          endif
          diff=20+(2*i+n)/2-yy(i*4+n+2+offset)
          if (abs(diff) > 1.0D-15) then
            write(*,*) 'Error in restricted array y(',i*4+n+2,')=',
     $  yy(i*4+n+2+offset),'!=',20+(2*i+n)/2
          endif
        enddo
      enddo
      call ceedvectorrestorearrayread(y,yy,offset,err)

      call ceedvectordestroy(x,err)
      call ceedvectordestroy(y,err)
      call ceedelemrestrictiondestroy(r,err)
      call ceeddestroy(ceed,err)

      end
c-----------------------------------------------------------------------
