
use TOML;
use Spawn;
use FileSystem;
use mason;

proc BuildProgram() {
  if isFile("Mason.lock") {
    if !isDir("target") {
      // Make Target Directory
      makeTargetFiles();
    }

    //Install dependencies into .mason/src
    var toParse = open("Mason.lock", iomode.r);
    var lockFile = parseToml(toParse);
    var sourceList = genSourceList(lockFile);
    getSrcCode(sourceList);

    // Compile Program
    if compileSrc(lockFile) {
      writeln("Build Successful");
    }
    else {
      writeln("Build Failed");
    }

    // Close memory
     delete lockFile;
     toParse.close();
  }
  else {
    writeln("Cannot build: no Mason.lock found");
  }
} 


/* Creates the rest of the project structure */
proc makeTargetFiles() {
  var makeTarget = "mkdir target";
  var makeDebug  = "mkdir target/debug";
  var makeTests = "mkdir target/debug/tests";
  var makeExamples = "mkdir target/debug/examples";
  var makeBenches = "mkdir target/debug/benches";
  runCommand(makeTarget);
  runCommand(makeDebug);
  runCommand(makeTests);
  runCommand(makeExamples);
  runCommand(makeBenches);
}


/* Compiles the program into the main project src
   folder. Requires that the main library file be
   named after the project folder in which it is 
   contained */
proc compileSrc(lockFile: Toml) : bool {
  var sourceList = genSourceList(lockFile);
  var depPath = getEnv("HOME") + '/.mason/src/';
  var project = lockFile["root"]["name"].s;
  var projectPath = 'src/'+ project + '.chpl';
 
  if isFile(projectPath) {
    var command: string = 'chpl ' + projectPath;
    for dep in sourceList {
      var depSrc = ' -M '+ depPath + dep(2) + '/src';
      command += depSrc;
    }
    runCommand(command); // compile Program with deps
  
    if isFile(project) {
      var moveFile = 'mv ' + project + ' target/debug/';
      runCommand(moveFile);
      if isFile('target/debug/' + project) {
	return true;
      }
    }
    return false;
  }
  else {
    writeln("Mason could not find your project!");
    return false;
  }
}



/* Generates a list of tuples that holds the git repo
   url and the name for local mason dependency pool */
proc genSourceList(lockFile: Toml) {
  var sourceList: [1..0] (string, string);
  for (name, package) in zip(lockFile.D, lockFile.A) {
    if package.tag == fieldToml {
      if name == "root" {
        continue;
      }
      else {
        var version = lockFile[name]["version"].s;
        var source = lockFile[name]["source"].s;
        sourceList.push_back((source, name+'-'+version));
      }
    }
  }
  return sourceList;
}

/* Clones the git repository of each dependency into
   the src code dependency pool */
proc getSrcCode(sourceList: [?d] 2*string) {
  var destination = getEnv("HOME") +'/.mason/src/';
  for source in sourceList {
    if !depExists(source(2)) {
      writeln("Downloading dependency: " + source(2));
      var getDependency = "git clone -q "+source(1)+' '+destination+source(2)+'/';
      runCommand(getDependency);
    }
  }
}


/* Checks to see if dependency has already been 
   downloaed previously */
proc depExists(dependency: string) {
  var repos = getEnv("HOME") +'/.mason/src/';
  var exists = false;
  for dir in listdir(repos) {
    if dir == dependency {
      exists = true;
    }
  }
  return exists;
}


proc runCommand(cmd) {
  var splitCmd = cmd.split();
  var process = spawn(splitCmd, stdout=PIPE);
  process.wait();

  for line in process.stdout.lines() {
    writeln(line);
  }
}