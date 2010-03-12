use driver_domains;

// test reindexing from distributed domains to non-distributed arrays

def foo(TD: domain, A: [TD] int, TA) {
  var errs = 0;
  var offset = if (TD.rank==1) then o5 else fill(TD.rank, o5);
  for i in [TD] do
    if A[i] != TA[i-offset] {
      writeln("A[",i,"] Incorrect reindex = ", A[i], ", TA[", i+offset, "] = ", TA[i+offset]);
      errs += 1;
    }
  return errs;
}

const TD1D: domain(1) = Space1 - (o5);
var TA1D: [TD1D] int;
for e in TA1D do e = next();
writeln("TA1D: ", foo(Dom1D, TA1D, TA1D), " errors");

const TD2D: domain(2) = Space2 - (o5, o5);
var TA2D: [TD2D] int;
for e in TA2D do e = next();
writeln("TA2D: ", foo(Dom2D, TA2D, TA2D), " errors");

const TD3D: domain(3) = Space3 - (o5, o5, o5);
var TA3D: [TD3D] int;
for e in TA3D do e = next();
writeln("TA3D: ", foo(Dom3D, TA3D, TA3D), " errors");

const TD4D: domain(4) = Space4 - (o5, o5, o5, o5);
var TA4D: [TD4D] int;
for e in TA4D do e = next();
writeln("TA4D: ", foo(Dom4D, TA4D, TA4D), " errors");

const TD2D64: domain(2,int(64)) = Space2D64 - (o5:int(64), o5:int(64));
var TA2D64: [TD2D64] int;
for e in TA2D64 do e = next();
writeln("TA2D64: ", foo(Dom2D64, TA2D64, TA2D64), " errors");
