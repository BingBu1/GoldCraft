package dev.goldcraft.tools;

import com.google.gson.*;
import cpw.mods.jarhandling.JarContents;
import cpw.mods.modlauncher.api.IEnvironment;
import java.io.*;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.function.Function;
import java.util.jar.JarEntry;
import java.util.jar.JarOutputStream;
import java.util.zip.ZipFile;
import net.neoforged.api.distmarker.Dist;
import net.neoforged.fml.ModLoadingIssue;
import net.neoforged.fml.loading.*;
import net.neoforged.fml.loading.moddiscovery.ModFile;
import net.neoforged.fml.loading.moddiscovery.ModFileInfo;
import net.neoforged.fml.loading.moddiscovery.readers.JarModsDotTomlModFileReader;
import net.neoforged.jarjar.selection.JarSelector;
import net.neoforged.neoforgespi.Environment;
import net.neoforged.neoforgespi.locating.ModFileDiscoveryAttributes;
import org.apache.maven.artifact.versioning.DefaultArtifactVersion;

/**
 * Offline metadata preflight using the unmodified, pinned FML 4.0.23 parser,
 * ModSorter and JarJarSelector 0.4.1. Does not load or execute third-party Mods.
 * Real JVM startup/gameplay remains a separate acceptance step.
 */
public final class NeoForgePackValidator {
    private static Path output;
    private record Candidate(Path path, int depth) {}
    private static final Gson JSON=new GsonBuilder().setPrettyPrinting().create();

    private static void environment(Dist side,JsonObject request) throws Exception {
        // IEnvironment.buildKey uses ModLauncher's class-bound key map.
        new cpw.mods.modlauncher.api.TypesafeMap(IEnvironment.class);
        var values=new HashMap<Object,Object>();
        values.put(Environment.Keys.DIST.get(),side);
        var env=(IEnvironment)Proxy.newProxyInstance(IEnvironment.class.getClassLoader(),new Class<?>[]{IEnvironment.class},(proxy,method,args)->switch(method.getName()) {
            case "getProperty" -> Optional.ofNullable(values.get(args[0]));
            case "computePropertyIfAbsent" -> values.computeIfAbsent(args[0],(Function<Object,Object>)args[1]);
            default -> throw new UnsupportedOperationException(method.getName());
        });
        Environment.build(env);
        // FML normally sets this in setupLaunchHandler. This standalone metadata
        // tool has no ModLauncher/game layer, so provide only that version input.
        var field=FMLLoader.class.getDeclaredField("versionInfo");field.setAccessible(true);
        field.set(null,new VersionInfo(request.get("neoforge").getAsString(),request.get("fml").getAsString(),request.get("minecraft").getAsString(),"20240613.152323"));
    }
    private static byte[] resource(Candidate candidate,Path relative) throws IOException {
        String name=relative.toString().replace('\\','/');
        if(relative.isAbsolute()||name.startsWith("/")||Arrays.asList(name.split("/")).contains(".."))throw new IOException("Invalid nested resource path");
        try(var zip=new ZipFile(candidate.path.toFile())) {
            var entry=zip.getEntry(name);if(entry==null)return null;
            long limit=name.endsWith(".jar")?128L*1024*1024:2L*1024*1024;
            if(entry.getSize()<0||entry.getSize()>limit)throw new IOException("Nested resource exceeds size limit");
            try(var input=zip.getInputStream(entry)){
                var bytes=input.readNBytes((int)limit+1);
                if(bytes.length>limit)throw new IOException("Expanded nested resource exceeds size limit");
                return bytes;
            }
        }
    }
    private static Optional<InputStream> resourceStream(Candidate parent,Path relative) {
        try{var bytes=resource(parent,relative);return bytes==null?Optional.empty():Optional.of(new ByteArrayInputStream(bytes));}
        catch(IOException e){throw new UncheckedIOException(e);}
    }
    private static Optional<Candidate> nested(Candidate parent,Path relative) {
        try{
            if(parent.depth>=12)throw new IOException("Nested jars are too deep");
            var bytes=resource(parent,relative);if(bytes==null)throw new IOException("Missing nested jar "+relative);
            var hash=HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
            var path=output.resolve("nested").resolve(hash+".jar");Files.createDirectories(path.getParent());
            if(!Files.exists(path))Files.write(path,bytes,StandardOpenOption.CREATE_NEW);
            return Optional.of(new Candidate(path,parent.depth+1));
        }catch(Exception e){throw new IllegalArgumentException("Cannot read nested jar: "+relative,e);}
    }
    private static Path platform(JsonObject request) throws IOException {
        Path path=output.resolve("platform.jar");
        String text="modLoader=\"javafml\"\nloaderVersion=\"[4,)\"\nlicense=\"test fixture\"\n";
        for(String id:List.of("minecraft","neoforge"))text+="[[mods]]\nmodId=\""+id+"\"\nversion=\""+request.get(id).getAsString()+"\"\n";
        try(var jar=new JarOutputStream(Files.newOutputStream(path))){
            jar.putNextEntry(new JarEntry("META-INF/neoforge.mods.toml"));jar.write(text.getBytes(StandardCharsets.UTF_8));jar.closeEntry();
        }
        return path;
    }
    private static JsonObject validate(String side,JsonObject request,Path platform) throws Exception {
        environment(side.equals("client")?Dist.CLIENT:Dist.DEDICATED_SERVER,request);
        var candidates=new ArrayList<Candidate>();
        for(var element:request.getAsJsonArray("jars")){
            var node=element.getAsJsonObject();
            if(node.getAsJsonArray("sides").asList().stream().anyMatch(value->value.getAsString().equals(side)))
                candidates.add(new Candidate(Path.of(node.get("path").getAsString()),0));
        }
        candidates.addAll(JarSelector.detectAndSelect(List.copyOf(candidates),NeoForgePackValidator::resourceStream,
            NeoForgePackValidator::nested,c->c.path.toString(),failures->new IllegalArgumentException("JarJar dependency conflict: "+failures)));
        candidates.add(new Candidate(platform,0));
        var mods=new ArrayList<ModFile>();
        var problems=new ArrayList<ModLoadingIssue>();
        var fmlVersion=new DefaultArtifactVersion(request.get("fml").getAsString());
        for(var candidate:candidates){
            var parsed=JarModsDotTomlModFileReader.createModFile(JarContents.of(candidate.path),ModFileDiscoveryAttributes.DEFAULT);
            if(parsed==null){if(candidate.depth==0)throw new IllegalArgumentException("No NeoForge metadata: "+candidate.path.getFileName());continue;}
            if(!(parsed.getModFileInfo() instanceof ModFileInfo info))continue; // Ordinary JarJar library.
            for(var language:info.requiredLanguageLoaders()){
                if(!Set.of("javafml","lowcodefml").contains(language.languageName()))
                    throw new IllegalArgumentException("Preflight does not support language provider "+language.languageName()+"; its actual FML provider must be integrated before deployment");
                if(!language.acceptedVersions().containsVersion(fmlVersion))
                    problems.add(ModLoadingIssue.error("Unsupported language loader version",language.languageName(),language.acceptedVersions(),fmlVersion));
            }
            mods.add((ModFile)parsed);
        }
        var result=ModSorter.sort(List.of(),mods,problems);
        var summary=new JsonObject();
        var errors=result.getModLoadingIssues().stream().filter(i->i.severity()==ModLoadingIssue.Severity.ERROR).map(Object::toString).toList();
        summary.addProperty("valid",errors.isEmpty());
        summary.add("issues",JSON.toJsonTree(result.getModLoadingIssues().stream().map(Object::toString).toList()));
        if(!errors.isEmpty())summary.addProperty("error",String.join("\n",errors));
        var resolved=new JsonArray();
        for(var mod:result.getMods()){
            var entry=new JsonObject();entry.addProperty("id",mod.getModId());entry.addProperty("version",mod.getVersion().toString());resolved.add(entry);
        }
        summary.add("resolved",resolved);return summary;
    }
    public static void main(String[] args) throws Exception {
        var request=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        Path result=Path.of(args[1]).toAbsolutePath().normalize();
        output=result.getParent();
        if(!output.toRealPath().startsWith(Path.of(request.get("workspace").getAsString()).resolve("sandbox").toRealPath()))throw new IllegalArgumentException("Validation output is outside sandbox");
        Path platform=platform(request);var report=new JsonObject();boolean valid=true;
        report.addProperty("validator","FML 4.0.23 ModFileParser/ModSorter; JarJarSelector 0.4.1");
        for(String side:List.of("client","server")){
            JsonObject checked;
            try{checked=validate(side,request,platform);}
            catch(Throwable error){checked=new JsonObject();checked.addProperty("valid",false);checked.addProperty("error",error.toString());error.printStackTrace();}
            valid&=checked.get("valid").getAsBoolean();report.add(side,checked);
        }
        report.addProperty("valid",valid);Files.writeString(result,JSON.toJson(report));
        if(!valid)System.exit(2);
    }
}
