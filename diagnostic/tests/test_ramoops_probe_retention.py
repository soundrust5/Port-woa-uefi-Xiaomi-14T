"""Compile the actual C producer and test retained/invalid/wrapped console rings."""
from pathlib import Path
import hashlib,json,subprocess,unittest
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/degas/ramoops-probe-retention-tests'
HARNESS=r'''
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ramoops.h"
unsigned diagnostic_ramoops(const uint64_t *record);
#define LEN 0x40000u
#define CAP (LEN-12u)
static unsigned char *z;
static uint64_t record[20];
static void check(int ok,const char *s){if(!ok){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static uint32_t *h(void){return (uint32_t*)z;}
static void logical(unsigned char *out){uint32_t start=h()[1],n=h()[2];if(n<CAP)memcpy(out,z+12,n);else for(uint32_t i=0;i<n;i++)out[i]=z[12+(start+i)%CAP];}
static size_t marker(unsigned char *out){
 memset(z,0,LEN);check(diagnostic_ramoops(record)==0,"fresh marker");size_t n=h()[2];logical(out);return n;
}
static void valid_case(size_t old_size,size_t start){
 unsigned char *expected=malloc(CAP+384),*actual=malloc(CAP),*msg=malloc(384);
 size_t n=marker(msg);memset(z,0,LEN);h()[0]=RAMOOPS_PERSISTENT_RAM_SIG;h()[1]=start;h()[2]=old_size;
 for(size_t i=0;i<CAP;i++)z[12+i]=(unsigned char)(i*37+19);
 logical(expected);memcpy(expected+old_size,msg,n);
 check(diagnostic_ramoops(record)==0,"append valid/wrapped");
 size_t total=old_size+n,kept=total>CAP?CAP:total;
 logical(actual);check(h()[2]==kept,"retained length");
 check(memcmp(actual,expected+(total-kept),kept)==0,"prior bytes plus new marker chronological");
 free(expected);free(actual);free(msg);
}
int main(void){
 z=mmap((void*)0x480af000,LEN,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 check(z==(void*)0x480af000,"map exact isolated process zone");
 record[3]=4;record[6]=0;record[14]=0x40000000;for(int i=0;i<32;i++)((unsigned char*)record)[128+i]=i;
 valid_case(0,0);valid_case(42,42);valid_case(CAP-5,CAP-5);valid_case(CAP,CAP-2);valid_case(CAP,CAP);valid_case(CAP,1);
 for(int which=0;which<3;which++){
  memset(z,0xa5,LEN);h()[0]=which==0?0:RAMOOPS_PERSISTENT_RAM_SIG;h()[1]=which==2?100:0;h()[2]=which==1?CAP+1:0;
  check(diagnostic_ramoops(record)==0,"invalid reset");check(h()[2]>0&&h()[2]<384,"invalid starts fresh");
 }
 unsigned char before[384],after[768];size_t n=marker(before);
 check(ramoops_console_append(z,LEN,"COHERENT RESULT RETAIN ME\n",25),"append previous CPU result");
 ((unsigned char*)record)[128]=0xff;
 check(diagnostic_ramoops(record)==0,"second UEFI entry");logical(after);
 check(!memcmp(after,before,n),"old build retained");
 check(!memcmp(after+n,"COHERENT RESULT RETAIN ME\n",25),"CPU result retained");
 check(!memcmp(after+n+25,"DEGAS-RAMOOPS-V1 BUILD=ff",23),"fresh build appended");
 puts("PASS: 6 valid ring positions, 3 corrupt headers, repeated entry retains CPU result");return 0;
}
'''
def wp(p):
 s=str(p.resolve()).replace('\\','/');return '/mnt/'+s[0].lower()+s[2:]
class RamoopsProbeRetention(unittest.TestCase):
 def test_actual_c_producer(self):
  OUT.mkdir(parents=True,exist_ok=True);src=OUT/'retention.c';exe=OUT/'retention';src.write_text(HARNESS)
  subprocess.run(['wsl','--','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',wp(ROOT/'diagnostic/include'),wp(src),wp(ROOT/'diagnostic/ramoops.c'),wp(ROOT/'diagnostic/ramoops_probe.c'),'-o',wp(exe)],check=True,capture_output=True)
  result=subprocess.run(['wsl','--',wp(exe)],check=True,capture_output=True,text=True)
  self.assertIn('PASS:',result.stdout)
 def test_regenerated_wrapper_provenance(self):
  d=ROOT/'diagnostic';m=json.loads((d/'build-mu-handoff/manifest.json').read_text())
  for name,digest in m['inputs'].items():self.assertEqual(hashlib.sha256((d/name).read_bytes()).hexdigest(),digest,name)
  b=(d/'build-mu-handoff/entry.bin').read_bytes();self.assertEqual(hashlib.sha256(b).hexdigest(),m['image_sha256'])
  self.assertIn(bytes.fromhex(m['build_id']),b)
if __name__=='__main__':unittest.main()
