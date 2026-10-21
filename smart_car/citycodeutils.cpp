#include "citycodeutils.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

CityCodeUtils::CityCodeUtils()
{

}

QString CityCodeUtils::getCityCodeFromName(QString cityName)
{
    if(cityMap.isEmpty()){
        initCityMap();
    }
    QMap<QString,QString>::iterator it = cityMap.find(cityName);
    if(it == cityMap.end()){
        it = cityMap.find(cityName + "市");
        if(it == cityMap.end()){
            it = cityMap.find(cityName + "县");
        }
        if(it == cityMap.end()){
            it = cityMap.find(cityName + "区");
        }
        if(it == cityMap.end()){
            return "";
        }
    }else{
        return it.value();
    }
    return it.value();
}

void CityCodeUtils::initCityMap()
{
    QFile file(":/citycode.json");
    file.open(QIODevice::ReadOnly);
    QByteArray rawData = file.readAll();
    file.close();

    QJsonDocument jsonDoc = QJsonDocument::fromJson(rawData);
    if(jsonDoc.isArray()){
        QJsonArray citys = jsonDoc.array();
        for(QJsonValue value : citys){
            if(value.isObject()){
                QString cityName = value["city_name"].toString();
                QString cityCode = value["city_code"].toString();
                cityMap.insert(cityName,cityCode);
            }
        }
    }
}
