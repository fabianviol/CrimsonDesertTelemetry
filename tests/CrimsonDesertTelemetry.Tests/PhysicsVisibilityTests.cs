using CrimsonDesertTelemetry.Core;
using System.IO.MemoryMappedFiles;

internal static class PhysicsVisibilityTests
{
    public static void Exchange()
    {
        var pid = Environment.ProcessId + 210000;
        const long born = 987654321;
        long now = 10000;
        using var results = MemoryMappedFile.CreateNew($"Local\\CrimsonDesertTelemetry.PhysicsVisibilityResultV2.{pid}",PhysicsVisibilityClient.BatchBytes);
        using var output = results.CreateViewAccessor();
        using var client = new PhysicsVisibilityClient(pid,born,()=>now);
        using var query = MemoryMappedFile.OpenExisting($"Local\\CrimsonDesertTelemetry.PhysicsVisibilityQueryV2.{pid}");
        using var input = query.CreateViewAccessor();
        var source = new RenderedLightSnapshot(4,new(11,21,31),new(2,.5f,.2f),.75f,"point",null,null);
        var camera = new CameraSnapshot(new(10,22,30),new(0,1,0),new(1,0,0),new(0,0,1),.1f,null,50,1.7f);
        var rendered = new RenderLightsSnapshot("available",RenderLightReader.SourceName,17,44,DateTimeOffset.UtcNow,10,camera,[source],new(1,1,0,0));
        var authored = new EngineLightSnapshot(new(9,21,29),"point",new(1,1,1),true,true,1,new(1,1,1));
        var snapshot = new EngineLightsSnapshot("available",EngineLightReader.SourceName,100,[authored],new(1,1,0,0,0,0,0,0,0,0),null,rendered);
        EngineLightsSnapshot Apply() => client.Apply((10,20,30),snapshot);
        SourceVisibilitySnapshot Visibility(EngineLightsSnapshot value) => value.Rendered!.Sources![0].SourceVisibility!;
        var first = Apply();
        Check(Visibility(first).Status=="unknown","initial unknown");
        const int header = PhysicsVisibilityClient.HeaderBytes;
        Check(input.ReadUInt32(0)==0x50564443 && input.ReadUInt32(4)==2 && input.ReadUInt32(8)==PhysicsVisibilityClient.BatchBytes && input.ReadUInt64(header+64)==17,"query ABI/provenance");
        Check(input.ReadSingle(header+104)==22 && input.ReadSingle(header+92)==20 && input.ReadSingle(header+112)==11,"paired camera, player and rendered-only target");
        void Reply(uint clear, uint code=1, bool wrongOrigin=false, int completedTargets=int.MaxValue)
        {
            var bytes = new byte[PhysicsVisibilityClient.BatchBytes]; input.ReadArray(0,bytes,0,bytes.Length);
            BitConverter.TryWriteBytes(bytes.AsSpan(0),0x53564443u);
            for(var n=0;n<BitConverter.ToUInt32(bytes,12);n++)
            {
                var o=header+n*128;
                BitConverter.TryWriteBytes(bytes.AsSpan(o),0x53564443u);
                BitConverter.TryWriteBytes(bytes.AsSpan(o+12),1u); BitConverter.TryWriteBytes(bytes.AsSpan(o+28),n<completedTargets ? code : 4u);
                BitConverter.TryWriteBytes(bytes.AsSpan(o+56),(ulong)now);
                BitConverter.TryWriteBytes(bytes.AsSpan(o+76),9u); BitConverter.TryWriteBytes(bytes.AsSpan(o+80),clear);
                if(wrongOrigin) BitConverter.TryWriteBytes(bytes.AsSpan(o+100),123f);
            }
            output.Write(16,1L); output.WriteArray(0,bytes,0,16); output.WriteArray(24,bytes,24,bytes.Length-24); output.Write(16,2L);
        }
        now+=60; Reply(0); var pending=Apply();
        Check(Visibility(pending).Status=="unknown" && Visibility(pending).Reason=="confirming-obstruction","one blocked sample must not hide");
        now+=60; Reply(0); var blocked=Apply();
        Check(Visibility(blocked) is {Status:"blocked",AttenuationFactor:0,Method:PhysicsVisibilityClient.Method,SampleCount:9,ClearSampleCount:0},"two complete blocked fans confirm");
        now+=60; Reply(4); var clear=Apply();
        Check(Visibility(clear) is {Status:"clear",AttenuationFactor:1,ClearSampleCount:4},"free neighbors restore immediately, not 4/9 attenuation");
        Check(clear.Sources![0]==authored && clear.Rendered!.Sources![0].ColorLinear==source.ColorLinear && clear.Rendered.Sources[0].LuminanceLinear==source.LuminanceLinear,"raw authored/rendered fields preserved");
        now+=60; Reply(0,wrongOrigin:true); Check(Visibility(Apply()).Status=="clear","wrong-origin result must not replace known result");
        snapshot=snapshot with { Rendered=rendered with {Camera=camera with {Position=new(10.3f,22,30)}} };
        Check(Visibility(Apply()).Status=="unknown","camera movement invalidates immediately");
        snapshot=snapshot with {Rendered=rendered}; now+=2600;
        Check(Visibility(Apply()).Status=="unknown","expired result cannot suppress raw light");
        // Regression: ALL queued lights refresh in one round while the receiver
        // moves. This failed by design with the old one-light-per-50ms scheduler.
        var many = Enumerable.Range(0,96).Select(n => source with { SampleIndex=n,
            Position=new(11+n%12,21,31+n/12) }).ToArray();
        snapshot=snapshot with {Rendered=rendered with {Sources=many}};
        now+=60; Reply(4); Apply();
        Check(input.ReadUInt32(12)==96,"96 nearby targets queued together, no old 24-light cutoff");
        var firstTarget=(input.ReadSingle(header+112),input.ReadSingle(header+116),input.ReadSingle(header+120));
        now+=60; Reply(4,completedTargets:1); var partial=Apply();
        Check(firstTarget!=(input.ReadSingle(header+112),input.ReadSingle(header+116),input.ReadSingle(header+120)),
            "budget-skipped tail prioritized before already checked target");
        Check(partial.Rendered!.Sources!.Any(s=>s.SourceVisibility!.Reason=="physics-budget-pending"),
            "untested targets are explicitly pending, never false clear");
        for(var step=1;step<=6;step++)
        {
            now+=60; Reply(0);
            snapshot=snapshot with {Rendered=snapshot.Rendered! with {Camera=camera with {Position=new(10+step*.2f,22,30)}}};
            var moving=Apply();
            if(step>=2) Check(moving.Rendered!.Sources!.All(s=>s.SourceVisibility!.Status=="blocked"),"moving scene has no per-light round-robin gap");
        }
        // A budget skip keeps only genuinely recent measurements, does not
        // invent a new tick or erase them, and remains first in retry order.
        var beforeSkip=Apply();
        now+=60; Reply(0,4); var skipped=Apply();
        Check(skipped.Rendered!.Sources![0].SourceVisibility!.LightCaptureSequence==beforeSkip.Rendered!.Sources![0].SourceVisibility!.LightCaptureSequence,
            "budget skip does not fabricate measurement provenance");
        Check(skipped.Rendered.Sources[0].SourceVisibility!.VolumeAgeMillisecondsAtCapture > beforeSkip.Rendered.Sources[0].SourceVisibility!.VolumeAgeMillisecondsAtCapture,
            "budget skip does not refresh age");
        now+=60; Reply(0,3); Check(Visibility(Apply()).Reason=="physics-stopped-restart-required","native fault exposed, not clear");
        var seq=input.ReadUInt64(24); now+=1000; Apply(); Check(input.ReadUInt64(24)==seq,"latched fault stops scheduling");
        snapshot=snapshot with {Rendered=rendered with {AgeMilliseconds=500}};
        Check(Apply().Rendered!.Sources![0].SourceVisibility is null,"stale paired source cannot keep classification");
    }
    private static void Check(bool ok,string message) { if(!ok) throw new InvalidOperationException(message); }
}
