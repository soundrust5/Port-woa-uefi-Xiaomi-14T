static UNICODE_STRING id(const char *text)
{
    static WCHAR storage[256];
    UNICODE_STRING s;
    size_t i,n=strlen(text);
    assert(n<256);
    for(i=0;i<n;++i)storage[i]=(unsigned char)text[i];
    s.Buffer=storage;s.Length=(uint16_t)(n*2);s.MaximumLength=s.Length;
    return s;
}
int main(void)
{
    UNICODE_STRING name=id("\\_SB.UNRELATED");
    PEP_PREPARE_DEVICE prepare={&name,TRUE};
    PEP_REGISTER_DEVICE reg={&name,(void *)1,NULL,(void *)99,PepDeviceAccepted};
    PEP_UNREGISTER_DEVICE handle;
    PEP_DEVICE_POWER_STATE power,before;
    PEP_POWER_CONTROL_REQUEST control;
    PEP_NOTIFY_COMPONENT_IDLE_STATE idle;
    PEP_WORK_INFORMATION info,untouched;
    PEP_WORK work;
    PEP_POWER_CONTROL_COMPLETE complete;
    unsigned int i;
    for(i=0;i<8;++i) { gCpus[i].Uid=i;gCpus[i].Domain=DegasDomainFromUid(i); }
    memset(&info,0xa5,sizeof(info));untouched=info;
    work.WorkInformation=&info;work.NeedWork=TRUE;
    assert(DegasAcceptDevice(PEP_DPM_WORK,&work));
    assert(!work.NeedWork && work.WorkInformation==&info && memcmp(&info,&untouched,sizeof(info))==0);
    gCpus[0].Registered=2;gCpus[0].Started=1;
    gCpus[0].KernelHandle=(void *)0x1234;gCpus[0].ConstraintPending=1;
    work.WorkInformation=NULL;work.NeedWork=TRUE;
    assert(DegasAcceptDevice(PEP_DPM_WORK,&work));
    assert(!work.NeedWork && !work.WorkInformation);
    assert(gCpus[0].ConstraintPending==1 && !gCpus[0].ConstraintInFlight);
    work.WorkInformation=&info;work.NeedWork=FALSE;
    assert(DegasAcceptDevice(PEP_DPM_WORK,&work));
    assert(work.NeedWork && work.WorkInformation==&info && info.WorkType==3);
    assert(info.PowerControl.DeviceHandle==(void *)0x1234);
    assert(info.PowerControl.PowerControlCode==&gConstraintGuid && info.PowerControl.RequestContext==&gCpus[0]);
    assert(!info.PowerControl.InBuffer && !info.PowerControl.OutBuffer && !info.PowerControl.InBufferSize && !info.PowerControl.OutBufferSize);
    assert(!gCpus[0].ConstraintPending && gCpus[0].ConstraintInFlight==1);
    gCpus[0].ConstraintPending=1;memset(&info,0xa5,sizeof(info));untouched=info;
    assert(DegasAcceptDevice(PEP_DPM_WORK,&work));
    assert(!work.NeedWork && work.WorkInformation==&info && memcmp(&info,&untouched,sizeof(info))==0);
    assert(gCpus[0].ConstraintPending==1 && gCpus[0].ConstraintInFlight==1);
    memset(&complete,0,sizeof(complete));complete.PowerControlCode=&gConstraintGuid;
    complete.RequestContext=&gCpus[0];complete.DeviceHandle=&gCpus[0];
    assert(DegasAcceptDevice(PEP_DPM_POWER_CONTROL_COMPLETE,&complete) && !gCpus[0].ConstraintInFlight);
    assert(DegasAcceptDevice(PEP_DPM_WORK,&work) && work.NeedWork && info.WorkType==3);
    assert(!gCpus[0].ConstraintPending && gCpus[0].ConstraintInFlight==1);
    assert(DegasAcceptDevice(PEP_DPM_POWER_CONTROL_COMPLETE,&complete));
    memset(gCpus,0,sizeof(gCpus));
    for(i=0;i<8;++i) { gCpus[i].Uid=i;gCpus[i].Domain=DegasDomainFromUid(i); }
    assert(DegasAcceptDevice(PEP_DPM_PREPARE_DEVICE,&prepare) && !prepare.DeviceAccepted);
    assert(DegasAcceptDevice(PEP_DPM_REGISTER_DEVICE,&reg));
    assert(reg.DeviceAccepted==PepDeviceNotAccepted && !reg.DeviceHandle);
    assert(DegasAcceptDevice(PEP_DPM_ABANDON_DEVICE,&prepare) && !prepare.DeviceAccepted);
    name.Buffer=NULL;name.Length=3;
    assert(DegasAcceptDevice(PEP_DPM_PREPARE_DEVICE,&prepare) && !prepare.DeviceAccepted);
    assert(DegasAcceptDevice(PEP_DPM_REGISTER_DEVICE,&reg) && reg.DeviceAccepted==PepDeviceNotAccepted);
    prepare.DeviceId=NULL;reg.DeviceId=NULL;
    assert(DegasAcceptDevice(PEP_DPM_PREPARE_DEVICE,&prepare) && !prepare.DeviceAccepted);
    assert(DegasAcceptDevice(PEP_DPM_REGISTER_DEVICE,&reg) && reg.DeviceAccepted==PepDeviceNotAccepted);
    name=id("\\_SB.C000");reg.DeviceId=&name;reg.KernelHandle=(void *)1;
    assert(DegasAcceptDevice(PEP_DPM_REGISTER_DEVICE,&reg));
    assert(reg.DeviceAccepted==PepDeviceAccepted && reg.DeviceHandle==(void *)&gCpus[0]);
    handle.DeviceHandle=reg.DeviceHandle;
    assert(DegasAcceptDevice(PEP_DPM_REGISTER_DEVICE,&reg)); /* Duplicate: handled decline. */
    assert(reg.DeviceAccepted==PepDeviceNotAccepted && !reg.DeviceHandle);
    assert(gCpus[0].Registered==2 && gCpus[0].KernelHandle==(void *)1);
    memset(&power,0xa5,sizeof(power));power.DeviceHandle=handle.DeviceHandle;
    before=power;
    assert(DegasAcceptDevice(PEP_DPM_DEVICE_POWER_STATE,&power));
    assert(memcmp(&power,&before,sizeof(power))==0); /* Every field is read-only. */
    memset(&idle,0,sizeof(idle));idle.DeviceHandle=handle.DeviceHandle;
    idle.Component=7;idle.IdleState=2;idle.DriverNotified=TRUE;
    assert(DegasAcceptDevice(PEP_DPM_NOTIFY_COMPONENT_IDLE_STATE,&idle) && idle.Completed);
    assert(idle.DeviceHandle==handle.DeviceHandle && idle.Component==7 && idle.IdleState==2 && idle.DriverNotified);
    assert(DegasAcceptDevice(PEP_DPM_DEVICE_STARTED,&handle) && gCpus[0].Started);
    memset(&control,0xa5,sizeof(control));control.DeviceHandle=handle.DeviceHandle;
    assert(DegasAcceptDevice(PEP_DPM_POWER_CONTROL_REQUEST,&control));
    assert(control.Status==STATUS_NOT_SUPPORTED && control.BytesReturned==0);
    assert(DegasAcceptDevice(PEP_DPM_UNREGISTER_DEVICE,&handle) && !gCpus[0].Registered);
    assert(rejected>=4 && gLastAcceptedUid==0 && notified>0);
    puts("PASS: actual DPM + work handlers: borrowed/poisoned/null/empty/inflight/retry buffers; ownership declines; input preservation");
    return 0;
}
