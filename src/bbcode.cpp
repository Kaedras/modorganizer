/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "bbcode.h"
#include <QRegularExpression>
#include <log.h>
#include <map>

using namespace Qt::StringLiterals;

namespace BBCode
{

namespace log = MOBase::log;

class BBCodeMap
{

  typedef std::map<QString, std::pair<QRegularExpression, QString>> TagMap;

public:
  static BBCodeMap& instance()
  {
    static BBCodeMap s_Instance;
    return s_Instance;
  }

  QString convertTag(QString input, int& length)
  {
    // extract the tag name
    auto match      = m_TagNameExp.match(input, 1, QRegularExpression::NormalMatch,
                                         QRegularExpression::AnchoredMatchOption);
    QString tagName = match.captured(0).toLower();
    TagMap::iterator tagIter = m_TagMap.find(tagName);
    if (tagIter != m_TagMap.end()) {
      // recognized tag
      if (tagName.endsWith('=')) {
        tagName.chop(1);
      }

      int closeTagPos        = 0;
      int nextTagPos         = 0;
      int nextTagSearchIndex = input.indexOf(']');
      int closeTagLength     = 0;
      if (tagName == '*') {
        // ends at the next bullet point
        static const QRegularExpression regex(
            uR"((\[\*\]|</ul>))"_s, QRegularExpression::CaseInsensitiveOption);
        closeTagPos = input.indexOf(regex, 3);
        // leave closeTagLength at 0 because we don't want to "eat" the next bullet
        // point
      } else if (tagName == "line") {
        // ends immediately after the tag
        closeTagPos = 6;
        // leave closeTagLength at 0 because there is no close tag to skip over
      } else {
        QRegularExpression nextTag(QStringLiteral("\\[%1[=\\]]?").arg(tagName),
                                   QRegularExpression::CaseInsensitiveOption);
        QString closeTag = QStringLiteral("[/%1]").arg(tagName);
        closeTagPos      = input.indexOf(closeTag, 0, Qt::CaseInsensitive);
        nextTagPos       = nextTag.match(input, nextTagSearchIndex).capturedStart(0);
        while (nextTagPos != -1 && closeTagPos != -1 && nextTagPos < closeTagPos) {
          closeTagPos        = input.indexOf(closeTag, closeTagPos + closeTag.size(),
                                             Qt::CaseInsensitive);
          nextTagSearchIndex = input.indexOf(u"]"_s, nextTagPos);
          nextTagPos = nextTag.match(input, nextTagSearchIndex).capturedStart(0);
        }
        if (closeTagPos == -1) {
          // workaround to improve compatibility: add fake closing tag
          input.append(closeTag);
          closeTagPos = input.size() - closeTag.size();
        }
        closeTagLength = closeTag.size();
      }

      if (closeTagPos > -1) {
        length       = closeTagPos + closeTagLength;
        QString temp = input.mid(0, length);
        tagIter->second.first.setPatternOptions(
            QRegularExpression::PatternOption::DotMatchesEverythingOption);
        auto match = tagIter->second.first.match(temp);
        if (match.hasMatch()) {
          if (tagIter->second.second.isEmpty()) {
            if (tagName == "color") {
              QString color   = match.captured(1);
              QString content = match.captured(2);
              if (color.at(0) == '#') {
                return temp.replace(
                    tagIter->second.first,
                    QStringLiteral("<font style=\"color: %1;\">%2</font>")
                        .arg(color, content));
              } else {
                auto colIter = m_ColorMap.find(color.toLower());
                if (colIter != m_ColorMap.end()) {
                  color = colIter->second;
                }
                return temp.replace(
                    tagIter->second.first,
                    QStringLiteral("<font style=\"color: #%1;\">%2</font>")
                        .arg(color, content));
              }
            } else {
              log::warn("don't know how to deal with tag {}", tagName);
            }
          } else {
            if (tagName == '*') {
              static const QRegularExpression regex(u"(\\[/\\*\\])?(<br/>)?$"_s);
              temp.remove(regex);
            }
            return temp.replace(tagIter->second.first, tagIter->second.second);
          }
        } else {
          // expression doesn't match. either the input string is invalid
          // or the expression is
          log::warn("{} doesn't match the expression for {}", temp, tagName);
          length = 0;
          return QString();
        }
      }
    }

    // not a recognized tag or tag invalid
    length = 0;
    return QString();
  }

private:
  BBCodeMap() : m_TagNameExp(u"[a-zA-Z*]*=?"_s)
  {
    m_TagMap[u"b"_s] =
        std::make_pair(QRegularExpression(u"\\[b\\](.*)\\[/b\\]"_s), "<b>\\1</b>");
    m_TagMap[u"i"_s] =
        std::make_pair(QRegularExpression(u"\\[i\\](.*)\\[/i\\]"_s), "<i>\\1</i>");
    m_TagMap[u"u"_s] =
        std::make_pair(QRegularExpression(u"\\[u\\](.*)\\[/u\\]"_s), "<u>\\1</u>");
    m_TagMap[u"s"_s] =
        std::make_pair(QRegularExpression(u"\\[s\\](.*)\\[/s\\]"_s), "<s>\\1</s>");
    m_TagMap[u"sub"_s] = std::make_pair(
        QRegularExpression(u"\\[sub\\](.*)\\[/sub\\]"_s), "<sub>\\1</sub>");
    m_TagMap[u"sup"_s] = std::make_pair(
        QRegularExpression(u"\\[sup\\](.*)\\[/sup\\]"_s), "<sup>\\1</sup>");
    m_TagMap[u"size="_s] =
        std::make_pair(QRegularExpression(u"\\[size=([^\\]]*)\\](.*)\\[/size\\]"_s),
                       "<font size=\"\\1\">\\2</font>");
    m_TagMap[u"color="_s] = std::make_pair(
        QRegularExpression(u"\\[color=([^\\]]*)\\](.*)\\[/color\\]"_s), "");
    m_TagMap[u"font="_s] =
        std::make_pair(QRegularExpression(u"\\[font=([^\\]]*)\\](.*)\\[/font\\]"_s),
                       "<font style=\"font-family: \\1;\">\\2</font>");
    m_TagMap[u"center"_s] =
        std::make_pair(QRegularExpression(u"\\[center\\](.*)\\[/center\\]"_s),
                       "<div align=\"center\">\\1</div>");
    m_TagMap[u"right"_s] =
        std::make_pair(QRegularExpression(u"\\[right\\](.*)\\[/right\\]"_s),
                       "<div align=\"right\">\\1</div>");
    m_TagMap[u"quote"_s] =
        std::make_pair(QRegularExpression(u"\\[quote\\](.*)\\[/quote\\]"_s),
                       "<figure class=\"quote\"><blockquote>\\1</blockquote></figure>");
    m_TagMap[u"quote="_s] =
        std::make_pair(QRegularExpression(u"\\[quote=([^\\]]*)\\](.*)\\[/quote\\]"_s),
                       "<figure class=\"quote\"><blockquote>\\2</blockquote></figure>");
    m_TagMap[u"spoiler"_s] =
        std::make_pair(QRegularExpression(u"\\[spoiler\\](.*)\\[/spoiler\\]"_s),
                       "<details><summary>Spoiler:  <div "
                       "class=\"bbc_spoiler_show\">Show</div></summary><div "
                       "class=\"spoiler_content\">\\1</div></details>");
    m_TagMap[u"code"_s] = std::make_pair(
        QRegularExpression(u"\\[code\\](.*)\\[/code\\]"_s), "<code>\\1</code>");
    m_TagMap[u"heading"_s] =
        std::make_pair(QRegularExpression(u"\\[heading\\](.*)\\[/heading\\]"_s),
                       "<h2><strong>\\1</strong></h2>");
    m_TagMap[u"line"_s] = std::make_pair(QRegularExpression(u"\\[line\\]"_s), "<hr>");

    // lists
    m_TagMap[u"list"_s] = std::make_pair(
        QRegularExpression(u"\\[list\\](.*)\\[/list\\]"_s), "<ul>\\1</ul>");
    m_TagMap[u"list="_s] = std::make_pair(
        QRegularExpression(u"\\[list.*\\](.*)\\[/list\\]"_s), "<ol>\\1</ol>");
    m_TagMap[u"ul"_s] =
        std::make_pair(QRegularExpression(u"\\[ul\\](.*)\\[/ul\\]"_s), "<ul>\\1</ul>");
    m_TagMap[u"ol"_s] =
        std::make_pair(QRegularExpression(u"\\[ol\\](.*)\\[/ol\\]"_s), "<ol>\\1</ol>");
    m_TagMap[u"li"_s] =
        std::make_pair(QRegularExpression(u"\\[li\\](.*)\\[/li\\]"_s), "<li>\\1</li>");

    // tables
    m_TagMap[u"table"_s] = std::make_pair(
        QRegularExpression(u"\\[table\\](.*)\\[/table\\]"_s), "<table>\\1</table>");
    m_TagMap[u"tr"_s] =
        std::make_pair(QRegularExpression(u"\\[tr\\](.*)\\[/tr\\]"_s), "<tr>\\1</tr>");
    m_TagMap[u"th"_s] =
        std::make_pair(QRegularExpression(u"\\[th\\](.*)\\[/th\\]"_s), "<th>\\1</th>");
    m_TagMap[u"td"_s] =
        std::make_pair(QRegularExpression(u"\\[td\\](.*)\\[/td\\]"_s), "<td>\\1</td>");

    // web content
    m_TagMap[u"url"_s] = std::make_pair(
        QRegularExpression(u"\\[url\\](.*)\\[/url\\]"_s), "<a href=\"\\1\">\\1</a>");
    m_TagMap[u"url="_s] =
        std::make_pair(QRegularExpression(u"\\[url=([^\\]]*)\\](.*)\\[/url\\]"_s),
                       "<a href=\"\\1\">\\2</a>");
    m_TagMap[u"img"_s] = std::make_pair(
        QRegularExpression(
            u"\\[img(?:\\s*width=\\d+\\s*,?\\s*height=\\d+)?\\](.*)\\[/img\\]"_s),
        "<img src=\"\\1\">");
    m_TagMap[u"img="_s] =
        std::make_pair(QRegularExpression(u"\\[img=([^\\]]*)\\](.*)\\[/img\\]"_s),
                       "<img src=\"\\2\" alt=\"\\1\">");
    m_TagMap[u"email="_s] = std::make_pair(
        QRegularExpression(u"\\[email=\"?([^\\]]*)\"?\\](.*)\\[/email\\]"_s),
        "<a href=\"mailto:\\1\">\\2</a>");
    m_TagMap[u"youtube"_s] =
        std::make_pair(QRegularExpression(u"\\[youtube\\](.*)\\[/youtube\\]"_s),
                       "<a "
                       "href=\"https://www.youtube.com/watch?v=\\1\">https://"
                       "www.youtube.com/watch?v=\\1</a>");

    // make all patterns non-greedy and case-insensitive
    for (TagMap::iterator iter = m_TagMap.begin(); iter != m_TagMap.end(); ++iter) {
      iter->second.first.setPatternOptions(
          QRegularExpression::CaseInsensitiveOption |
          QRegularExpression::InvertedGreedinessOption);
    }

    // this tag is in fact greedy
    m_TagMap[u"*"_s] =
        std::make_pair(QRegularExpression(u"\\[\\*\\](.*)"_s), "<li>\\1</li>");

    m_ColorMap.insert(std::make_pair<QString, QString>(u"red"_s, u"FF0000"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"green"_s, u"00FF00"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"blue"_s, u"0000FF"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"black"_s, u"000000"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"gray"_s, u"7F7F7F"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"white"_s, u"FFFFFF"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"yellow"_s, u"FFFF00"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"cyan"_s, u"00FFFF"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"magenta"_s, u"FF00FF"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"brown"_s, u"A52A2A"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"orange"_s, u"FFA500"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"gold"_s, u"FFD700"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"deepskyblue"_s, u"00BFFF"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"salmon"_s, u"FA8072"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"dodgerblue"_s, u"1E90FF"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"greenyellow"_s, u"ADFF2F"_s));
    m_ColorMap.insert(std::make_pair<QString, QString>(u"peru"_s, u"CD853F"_s));
  }

private:
  QRegularExpression m_TagNameExp;
  TagMap m_TagMap;
  std::map<QString, QString> m_ColorMap;
};

QString convertToHTML(const QString& inputParam)
{
  // this code goes over the input string once and replaces all bbtags
  // it encounters. This function is called recursively for every replaced
  // string to convert nested tags.
  //
  // This could be implemented simpler by applying a set of regular expressions
  // for each recognized bb-tag one after the other but that would probably be
  // very inefficient (O(n^2)).

  QString input = inputParam.mid(0).replace("\r\n", "<br/>");
  input.replace("\\\"", "\"").replace("\\'", "'");
  QString result;
  int lastBlock = 0;
  int pos       = 0;

  // iterate over the input buffer
  while ((pos = input.indexOf('[', lastBlock)) != -1) {
    // append everything between the previous tag-block and the current one
    result.append(input.mid(lastBlock, pos - lastBlock));

    if ((pos < (input.size() - 1)) && (input.at(pos + 1) == '/')) {
      // skip invalid end tag
      int tagEnd = input.indexOf(']', pos) + 1;
      if (tagEnd == 0) {
        // no closing tag found
        // move the pos up one so that the opening bracket is ignored next iteration
        pos++;
      } else {
        pos = tagEnd;
      }
    } else {
      // convert the tag and content if necessary
      int length          = -1;
      QString replacement = BBCodeMap::instance().convertTag(input.mid(pos), length);
      if (length != 0) {
        result.append(convertToHTML(replacement));
        // length contains the number of characters in the original tag
        pos += length;
      } else {
        // nothing replaced
        result.append('[');
        ++pos;
      }
    }
    lastBlock = pos;
  }

  // append the remainder (everything after the last tag)
  result.append(input.mid(lastBlock));
  return result;
}

}  // namespace BBCode
