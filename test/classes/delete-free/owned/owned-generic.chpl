class GenericClass {
  type t;
  var v: int;
}

proc f(x: owned GenericClass) {
  writeln(x);
}

var a = new owned GenericClass(int, 3);
var b = new owned GenericClass(real, 4);
f(a);
f(b);
