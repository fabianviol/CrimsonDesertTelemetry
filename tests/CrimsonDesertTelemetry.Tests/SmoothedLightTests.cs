using System.Diagnostics;
using System.Text.Json;
using CrimsonDesertTelemetry.Core;
using CrimsonDesertTelemetry.Cli;

internal static class SmoothedLightTests
{
    private static readonly DateTimeOffset Start = DateTimeOffset.Parse("2026-09-07T20:00:00Z");
    private static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    private static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }
    private static bool Near(float a, double b) => Math.Abs(a - b) < .0001;
    private static RenderedLightSnapshot Light(int id, float x, float r = 1, float y = 0) =>
        new(id, new(x,y,0), new(r,0,0), r*.212671f, "point", null, null);
    private static RenderLightsSnapshot Input(ulong sequence, double ms, params RenderedLightSnapshot[] lights) =>
        new("available", "filtered-manylights", sequence, (uint)sequence, Start.AddMilliseconds(ms), 0, null,
            lights, new(lights.Length, lights.Length, 0, 0));
    private static SmoothedLightsSnapshot Step(SmoothedLightProcessor p, ulong sequence, double ms, params RenderedLightSnapshot[] lights) =>
        p.Process(Input(sequence, ms, lights), Start.AddMilliseconds(ms));

    public static void Grouping()
    {
        var p = new SmoothedLightProcessor();
        var input = Input(1, 0, Light(1,0,2),Light(2,0,3,.05f),Light(3,2,4));
        var original = JsonSerializer.Serialize(input, Json);
        var result = p.Process(input, Start);
        Check(result.Sources!.Count==2, "stacked contributions/singleton grouping");
        var group=result.Sources[0];
        Check(group.Contributions.Count==2 && group.ColorLinear.X==5 && group.RawSumColorLinear.X==5, "must sum, not average/clamp linear HDR");
        Check(Near(group.Position.Y,.025) && Near(group.LuminanceLinear,5*.212671), "centroid/luminance");
        Check(result.Sources[1].ColorLinear.X==4, "singleton changed on first observation");
        Check(JsonSerializer.Serialize(input, Json)==original, "raw input mutated");
        var chain = Step(new(),1,0,Light(0,0),Light(1,.14f),Light(2,.28f));
        Check(chain.Sources!.Count==2, "transitive chain joined separate lamps");
        var spots = Step(new(),1,0,Light(0,0) with { Kind="spot",Direction=new(1,0,0),ConeHalfAngleDegrees=30 },
            Light(1,.04f) with { Kind="spot",Direction=new(-1,0,0),ConeHalfAngleDegrees=60 });
        Check(spots.Sources![0].Contributions[0].Direction!.Value.X==1 &&
            spots.Sources[0].Contributions[1].Direction!.Value.X==-1, "opposing directions lost or averaged");
    }
    public static void Smoothing()
    {
        var p=new SmoothedLightProcessor(new(200));
        var first=Step(p,1,0,Light(5,0,1),Light(6,.05f,1),Light(7,2,2));
        var next=Step(p,2,200,Light(88,2,4),Light(91,.05f,2),Light(90,0,2));
        var alpha=1-Math.Exp(-1);
        Check(Near(next.Sources![0].ColorLinear.X,2+alpha*2) && Near(next.Sources[1].ColorLinear.X,2+alpha*2), "group and singleton EMA");
        Check(first.Sources![0].TrackingId==next.Sources[0].TrackingId && first.Sources[1].TrackingId==next.Sources[1].TrackingId,
            "ephemeral sample indices used as identities");
        var repeated=p.Process(Input(2,200,Light(88,2,4),Light(91,.05f,2),Light(90,0,2)),Start.AddMilliseconds(250));
        Check(repeated.Sources![0].ColorLinear==next.Sources[0].ColorLinear && repeated.AgeMilliseconds==50, "duplicate capture re-smoothed/refreshed");
        var disabled = new SmoothedLightProcessor(new(0)); Step(disabled,1,0,Light(0,0));
        Check(Step(disabled,2,100,Light(8,0,5)).Sources![0].ColorLinear.X==5, "zero time constant did not disable smoothing");
        var frequent = new SmoothedLightProcessor(new(200)); Step(frequent,1,0,Light(0,0));
        SmoothedLightsSnapshot frequentResult=null!;
        for (ulong n=1;n<=4;n++) frequentResult=Step(frequent,n+1,n*50,Light(3,0,3));
        var sparse = new SmoothedLightProcessor(new(200)); Step(sparse,1,0,Light(0,0));
        var sparseResult=Step(sparse,2,200,Light(4,0,3));
        Check(Near(frequentResult.Sources![0].ColorLinear.X,sparseResult.Sources![0].ColorLinear.X), "rate-dependent smoothing");
    }
    public static void Freshness()
    {
        var p=new SmoothedLightProcessor(); var initial=Step(p,1,0,Light(0,0,5));
        var empty=Step(p,2,50);
        Check(empty.Status=="available" && empty.Sources!.Count==0, "missing group left ghost/decay tail");
        var again=Step(p,3,100,Light(0,0,1));
        Check(again.Sources![0].TrackingId!=initial.Sources![0].TrackingId && again.Sources[0].ColorLinear.X==1, "reappearing lamp reused absent state");
        var stale=p.Process(Input(3,100,Light(0,0,1)),Start.AddMilliseconds(601));
        Check(stale.Status=="unavailable" && stale.Sources is null && stale.CapturedAt is null,"stale data retained");
        var fresh=Step(p,4,650,Light(0,0,9)); Check(fresh.Sources![0].ColorLinear.X==9,"stale state smoothed into new source");
        Check(p.Process(RenderLightReader.Unavailable("bridge-changing"),Start.AddMilliseconds(660)).Sources is null,"bridge fault not cleared");
        var restarted=Step(p,1,700,Light(0,0,2));
        Check(restarted.Sources![0].ColorLinear.X==2 && restarted.Sources[0].TrackingId!=fresh.Sources[0].TrackingId,"restart identity/state retained");
        var changed=Step(p,1,700,Light(0,0,3));
        Check(changed.Status=="unavailable","same capture with changed data passed");
    }
    public static void ValidationAndBounds()
    {
        foreach(var options in new[]{new LightSmoothingOptions(double.NaN),new(-1),new(2001),new(200,float.NaN),new(200,0),new(200,2)})
        {
            var rejected=false; try { _=new SmoothedLightProcessor(options); } catch(ArgumentOutOfRangeException) { rejected=true; }
            Check(rejected,"invalid smoothing settings accepted");
        }
        foreach(var invalid in new[]{Light(0,float.NaN),Light(-1,0),Light(0,0,-1),Light(0,0,float.PositiveInfinity)})
            Check(Step(new(),1,0,invalid).Status=="unavailable","invalid source accepted");
        Check(Step(new(),1,0,Light(0,0),Light(0,1)).Status=="unavailable","duplicate index accepted");
        var p=new SmoothedLightProcessor(); var timer=Stopwatch.StartNew();
        var dense=Enumerable.Range(0,RenderLightReader.RawCount).Select(i=>Light(i,0)).ToArray();
        var result=Step(p,1,0,dense);
        Check(result.Sources!.Count==1 && result.Sources[0].Contributions.Count==RenderLightReader.RawCount &&
            result.Sources[0].ColorLinear.X==RenderLightReader.RawCount,"bounded dense pool lost contributions");
        Check(timer.Elapsed < TimeSpan.FromSeconds(5),"dense-pool grouping unbounded/quadratic");
        var separated=Enumerable.Range(0,4096).Select(i=>Light(i,i*2)).ToArray();
        var a=Step(p,2,50,separated); var b=Step(p,3,100,separated.Reverse().ToArray());
        Check(a.Sources!.Count==4096 && a.Sources.Select(g=>g.TrackingId).SequenceEqual(b.Sources!.Select(g=>g.TrackingId)),"large stable group tracking");
    }
    public static void TransportIsolation()
    {
        var state=new TelemetryServerState(Json,60,"1.4");
        using var raw=state.Subscribe(); using var smooth=state.Subscribe(true);
        Check(state.LatestSmoothed.Status=="unavailable","startup must not invent light data");
        var render=Input(1,0,Light(0,0),Light(1,.04f)) with { CapturedAt=DateTimeOffset.UtcNow };
        var lights=new EngineLightsSnapshot("available","authored",100,[],new(0,0,0,0,0,0,0,0,0,0),Rendered:render);
        var snapshot=new TelemetrySnapshot("1.4",7,DateTimeOffset.UtcNow,new("test","playing"),new("game-unit","right","y"),[],null,null,null,lights);
        var expected=JsonSerializer.SerializeToUtf8Bytes(snapshot,Json);
        state.Publish(snapshot,1,0);
        Check(raw.Reader.TryRead(out var rawBytes) && rawBytes.SequenceEqual(expected),"raw transport payload changed");
        Check(smooth.Reader.TryRead(out var smoothBytes),"derived subscription empty");
        var derived=JsonSerializer.Deserialize<SmoothedLightsSnapshot>(smoothBytes!,Json)!;
        Check(derived.Sources!.Count==1 && derived.Sources[0].ColorLinear.X==2,"wrong derived transport");
        state.SetHealth("error",true,true,"test",0,0,"test failure");
        Check(state.LatestSmoothed.Status=="unavailable" && state.LatestSmoothed.Sources is null,"health-only error retained smoothed output");
        Check(smooth.Reader.TryRead(out var fault) && JsonSerializer.Deserialize<SmoothedLightsSnapshot>(fault,Json)!.Status=="unavailable", "fault not streamed");
        Check(!raw.Reader.TryRead(out _) && state.LatestBytes!.SequenceEqual(expected),"derived fault changed raw endpoint semantics");
        Check(state.Health.ConnectedClients==2,"subscriptions not counted");
    }
}
