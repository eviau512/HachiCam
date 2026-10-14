package com.scanner.app.data.image

import java.io.*
import java.util.zip.CRC32

/** Small committed metadata only; images remain immutable files outside this index. */
class PageIndexStore(private val root: File) {
    data class Record(val id:String,val source:String,val processed:String?,val thumbnail:String?,
        val filter:String,val rotation:Int,val aspect:Float?,val createdAt:Long,val status:String,
        val quad:List<Float>,val curves:List<List<Float>>)
    companion object { const val PAGE_LIMIT=512;const val BYTE_LIMIT=2*1024*1024;private const val MAGIC=0x48435031 }
    private val current=File(root,"pages.index")
    private val previous=File(root,"pages.previous")
    private fun name(value:String)=value.isNotBlank() && value.length<=200 && value !in listOf(".","..") &&
        !value.contains('/') && !value.contains('\\') && !value.contains(':')
    private fun validate(records:List<Record>) {
        require(records.size<=PAGE_LIMIT && records.map { it.id }.distinct().size==records.size)
        records.forEach { r ->
            require(r.id.isNotBlank() && r.id.length<=80 && name(r.source) && (r.processed==null || name(r.processed)) && (r.thumbnail==null || name(r.thumbnail)))
            require(r.filter in setOf("ORIGINAL","MAGIC_COLOR","PAPER") && r.rotation in setOf(0,90,180,270))
            require(r.status in setOf("PROCESSING","READY","FAILED") && (r.aspect==null || r.aspect.isFinite() && r.aspect in .05f..20f))
            require(r.quad.isEmpty() || r.quad.size==8)
            require(r.curves.isEmpty() || r.curves.size==4)
            require(r.curves.all { it.size in 4..8192 && it.size%2==0 })
            require((r.quad+r.curves.flatten()).all { it.isFinite() && it in -65536f..65536f })
        }
    }
    private fun encode(records:List<Record>):ByteArray {
        validate(records)
        val bytes=ByteArrayOutputStream()
        DataOutputStream(bytes).use { out ->
            out.writeInt(records.size)
            records.forEach { r ->
                out.writeUTF(r.id);out.writeUTF(r.source);out.writeUTF(r.processed.orEmpty());out.writeUTF(r.thumbnail.orEmpty())
                out.writeUTF(r.filter);out.writeInt(r.rotation);out.writeBoolean(r.aspect!=null);r.aspect?.let(out::writeFloat)
                out.writeLong(r.createdAt);out.writeUTF(r.status)
                fun points(p:List<Float>){out.writeInt(p.size);p.forEach(out::writeFloat)}
                points(r.quad);out.writeInt(r.curves.size);r.curves.forEach(::points)
                require(bytes.size()<=BYTE_LIMIT)
            }
        }
        return bytes.toByteArray().also { require(it.size<=BYTE_LIMIT) }
    }
    private fun read(file:File):List<Record> {
        require(file.length() in 20..(BYTE_LIMIT+20).toLong())
        val payload=DataInputStream(file.inputStream().buffered()).use { input ->
            require(input.readInt()==MAGIC && input.readInt()==1)
            val size=input.readInt();val crc=input.readLong();require(size in 4..BYTE_LIMIT)
            ByteArray(size).also { input.readFully(it);require(input.read()==-1 && CRC32().apply { update(it) }.value==crc) }
        }
        return DataInputStream(ByteArrayInputStream(payload)).use { input ->
            val count=input.readInt();require(count in 0..PAGE_LIMIT)
            List(count) {
                val id=input.readUTF();val source=input.readUTF();val processed=input.readUTF().ifEmpty { null };val thumb=input.readUTF().ifEmpty { null }
                val filter=input.readUTF();val rotation=input.readInt();val aspect=if(input.readBoolean())input.readFloat() else null
                val created=input.readLong();val status=input.readUTF()
                fun points():List<Float>{val n=input.readInt();require(n in 0..8192);return List(n){input.readFloat()}}
                val quad=points();val curves=input.readInt();require(curves==0 || curves==4)
                Record(id,source,processed,thumb,filter,rotation,aspect,created,status,quad,List(curves){points()})
            }.also { require(input.read()==-1);validate(it) }
        }
    }
    fun load():List<Record> {
        if(!current.exists() && !previous.exists())return emptyList()
        runCatching { return read(current) }
        return read(previous) // Never silently overwrite an unreadable existing index.
    }
    fun save(records:List<Record>) {
        val payload=encode(records)
        check(root.mkdirs() || root.isDirectory)
        val pending=File(root,"pages-${java.util.UUID.randomUUID()}.pending")
        try {
            FileOutputStream(pending).use { file ->
                val out=DataOutputStream(file);out.writeInt(MAGIC);out.writeInt(1);out.writeInt(payload.size)
                out.writeLong(CRC32().apply { update(payload) }.value);out.write(payload);out.flush();file.fd.sync()
            }
            // Copy the validated previous checkpoint before replacing current.
            // Keeping it is safe because the repository pins its image paths.
            if(current.exists()) {
                runCatching { read(current) }.getOrNull()?.let {
                    val backup=File(root,"pages-backup.pending")
                    try { FileOutputStream(backup).use { destination ->
                            current.inputStream().use { it.copyTo(destination) };destination.fd.sync()
                        }
                        java.nio.file.Files.move(backup.toPath(),previous.toPath(),
                            java.nio.file.StandardCopyOption.ATOMIC_MOVE,java.nio.file.StandardCopyOption.REPLACE_EXISTING)
                    } finally { backup.delete() }
                }
            }
            java.nio.file.Files.move(pending.toPath(),current.toPath(),
                java.nio.file.StandardCopyOption.ATOMIC_MOVE,java.nio.file.StandardCopyOption.REPLACE_EXISTING)
        } finally { pending.delete() }
    }
}
