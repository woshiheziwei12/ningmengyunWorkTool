#include "Jet3Reader.h"
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCodec>
#include <QTextStream>
#include <QSet>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <mdbtools.h>

static QString decode(QTextCodec* codec, const QByteArray& data)
{
    QTextCodec::ConverterState state;
    auto value=codec->toUnicode(data.constData(),data.size(),&state);
    if(state.invalidChars || state.remainingChars) throw std::runtime_error("Invalid source text encoding");
    return value.isNull() ? QStringLiteral("") : value;
}

static QByteArray longValue(MdbHandle* db, const MdbColumn& column)
{
    if(column.cur_value_len<12) throw std::runtime_error("Invalid long-value header");
    const auto header=QByteArray(reinterpret_cast<char*>(db->pg_buf)+column.cur_value_start,column.cur_value_len);
    const quint32 descriptor=mdb_get_int32(const_cast<char*>(header.constData()),0);
    const quint32 expected=descriptor & 0x3fffffff;
    if(expected>64*1024*1024) throw std::runtime_error("Long value exceeds recovery limit");
    QByteArray bytes;
    if(descriptor & 0x80000000) bytes=header.mid(12);
    else {
        quint32 next=mdb_get_int32(const_cast<char*>(header.constData()),4);
        QSet<quint32> visited;
        while(next) {
            if(visited.contains(next)) throw std::runtime_error("Cyclic long-value chain");
            visited.insert(next);
            void* buffer=nullptr; int offset=0; size_t size=0;
            if(mdb_find_pg_row(db,next,&buffer,&offset,&size)) throw std::runtime_error("Unreadable long-value page");
            if(descriptor & 0x40000000) { bytes.append(static_cast<char*>(buffer)+offset,int(size)); next=0; }
            else {
                if(size<4) throw std::runtime_error("Short long-value page");
                next=mdb_get_int32(buffer,offset);
                bytes.append(static_cast<char*>(buffer)+offset+4,int(size)-4);
            }
            if(bytes.size()>int(expected)) throw std::runtime_error("Long-value size overflow");
        }
    }
    if(bytes.size()!=int(expected)) throw std::runtime_error("Long-value size mismatch");
    return bytes;
}

static QJsonValue readValue(MdbHandle* db, QTextCodec* codec, const MdbColumn& col)
{
    if(col.col_type==MDB_BOOL) return col.cur_value_len==0;
    if(col.cur_value_is_null) return QJsonValue(QJsonValue::Null);
    const int offset=col.cur_value_start, size=col.cur_value_len;
    if(offset<0 || size<0 || offset>db->fmt->pg_size-size) throw std::runtime_error("Field outside page");
    const QByteArray raw(reinterpret_cast<char*>(db->pg_buf)+offset,size);
    switch(col.col_type) {
    case MDB_BYTE: if(size==1) return int(mdb_get_byte(db->pg_buf,offset)); break;
    case MDB_INT: if(size==2) return int(qint16(mdb_get_int16(db->pg_buf,offset))); break;
    case MDB_LONGINT: if(size==4) return int(qint32(mdb_get_int32(db->pg_buf,offset))); break;
    case MDB_MONEY: if(size==8) {
        std::unique_ptr<char,decltype(&free)> text(mdb_money_to_string(db,offset),free);
        if(!text) throw std::runtime_error("Currency decode failed");
        return QString::fromLatin1(text.get());
    } break;
    case MDB_FLOAT: case MDB_DOUBLE: case MDB_DATETIME:
        if(size==(col.col_type==MDB_FLOAT?4:8)) {
            const double value=col.col_type==MDB_FLOAT?mdb_get_single(db->pg_buf,offset):mdb_get_double(db->pg_buf,offset);
            if(!std::isfinite(value)) throw std::runtime_error("Non-finite number");
            return QString::number(value,'g',17);
        } break;
    case MDB_BINARY: return QString::fromLatin1(raw.toBase64());
    case MDB_TEXT: return decode(codec,raw);
    case MDB_OLE: return QString::fromLatin1(longValue(db,col).toBase64());
    case MDB_MEMO: return decode(codec,longValue(db,col));
    }
    throw std::runtime_error("Unsupported field type or length");
}

QJsonObject readJet3Snapshot(const QString& sourcePath)
{
    const auto size=QFileInfo(sourcePath).size();
    if(size<4096 || size%2048 || size>256*1024*1024) throw std::runtime_error("Jet3 file is truncated or exceeds 256 MiB");
        const auto path = sourcePath.toUtf8();
        std::unique_ptr<MdbHandle, decltype(&mdb_close)> db(mdb_open(path.constData(), MDB_NOFLAGS), mdb_close);
        if (!db || db->f->jet_version != MDB_VER_JET3) throw std::runtime_error("Not a readable Jet3 file");
        auto codec = QTextCodec::codecForName(QByteArray("CP") + QByteArray::number(db->f->code_page));
        if (!codec) throw std::runtime_error("Unknown codepage");
        auto catalog = mdb_read_catalog(db.get(), MDB_TABLE);
        if (!catalog || mdb_read_failed(db.get())) throw std::runtime_error("Catalog scan failed");
        QJsonArray tables;
        int rowCount = 0;
        qint64 cellCount = 0;
        QJsonObject typeCounts;
        for (unsigned i=0; i<catalog->len; ++i) {
            auto entry = static_cast<MdbCatalogEntry*>(g_ptr_array_index(catalog,i));
            const auto name = decode(codec,QByteArray(entry->object_name));
            if (name.startsWith("MSys", Qt::CaseInsensitive)) continue;
            std::unique_ptr<MdbTableDef, decltype(&mdb_free_tabledef)> table(mdb_read_table(entry),mdb_free_tabledef);
            if (!table || !mdb_read_columns(table.get())) throw std::runtime_error("Table schema failed");
            if(table->num_cols>256 || table->num_idxs>1024) throw std::runtime_error("Invalid schema size");
            QJsonArray columns;
            for (unsigned c=0;c<table->columns->len;++c) {
                auto col = static_cast<MdbColumn*>(g_ptr_array_index(table->columns,c));
                if(col->col_type<1 || col->col_type>12) throw std::runtime_error("Unsupported source column type");
                columns.append(QJsonObject{{"name",decode(codec,QByteArray(col->name))}, {"type",col->col_type},
                    {"size",col->col_size},{"auto",bool(col->is_long_auto)}, {"scale",col->col_scale}, {"precision",col->col_prec}});
            }
            QJsonArray indexes;
            if(table->num_idxs) {
                mdb_read_indices(table.get()); // 1.0.1 returns NULL even on success; inspect the populated array.
                if(!table->indices || mdb_read_failed(db.get())) throw std::runtime_error(("Index schema failed: "+name).toUtf8().constData());
            }
            if(table->indices) for(unsigned n=0;n<table->indices->len;++n) {
                auto idx=static_cast<MdbIndex*>(g_ptr_array_index(table->indices,n));
                if(idx->num_keys>MDB_MAX_IDX_COLS) throw std::runtime_error("Invalid index key count");
                QJsonArray keys;
                for(unsigned k=0;k<idx->num_keys;++k) {
                    if(idx->key_col_num[k]<1 || unsigned(idx->key_col_num[k])>table->num_cols)
                        throw std::runtime_error("Index references an invalid column");
                    keys.append(QJsonObject{{"column",idx->key_col_num[k]},{"order",idx->key_col_order[k]}});
                }
                indexes.append(QJsonObject{{"name",decode(codec,QByteArray(idx->name))},{"type",idx->index_type},{"flags",idx->flags},{"keys",keys}});
            }
            int rows=0;
            QJsonArray records;
            mdb_rewind_table(table.get());
            while (mdb_fetch_row(table.get())) {
                ++rows;
                cellCount += table->num_cols;
                if(rowCount+rows>500000 || cellCount>4000000) throw std::runtime_error("Recovery exceeds supported row/cell limit");
                QJsonArray record;
                for (unsigned c=0;c<table->columns->len;++c) {
                    auto col=static_cast<MdbColumn*>(g_ptr_array_index(table->columns,c));
                    try { record.append(readValue(db.get(),codec,*col)); }
                    catch(const std::exception& error) {
                        throw std::runtime_error((name+"/"+decode(codec,QByteArray(col->name))+" row="+QString::number(rows)+": "+error.what()).toUtf8().constData());
                    }
                    if (!col->cur_value_is_null) {
                        auto key=QString::number(col->col_type);
                        typeCounts[key]=typeCounts[key].toInt()+1;
                    }
                }
                records.append(record);
            }
            if (mdb_read_failed(db.get())) throw std::runtime_error("Incomplete table scan");
            tables.append(QJsonObject{{"name",name},{"rows",rows},{"headerRows",int(table->num_rows)},{"columns",columns},{"indexes",indexes},{"records",records}});
            rowCount+=rows;
        }
        return QJsonObject{{"codepage",int(db->f->code_page)},{"tables",tables},{"rowCount",rowCount},{"nonNullTypes",typeCounts}};
}
